#include <cms/api/verdict_routes.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/select.h>

#include <drogon/drogon.h>
#include <libpq-fe.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/authorize.hpp>
#include <cms/api/http.hpp>
#include <cms/api/route_text.hpp>
#include <cms/api/task_view.hpp>
#include <cms/api/verdict_view.hpp>
#include <cms/api/web.hpp>
#include <cms/capability.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

constexpr std::string_view SIGN_IN_PATH = "/signin";
constexpr std::string_view VERDICT_CHANNEL = "cms_verdict";
constexpr unsigned STREAM_SECONDS = 20;
constexpr unsigned STREAMS_PER_SESSION = 2;

/// The newest submission the viewer made for this task version, or "nothing yet" when there is none or it is
/// still unjudged. One read: a verdict write is atomic, so a row either has one and its time or neither.
auto latest_verdict(db::connection& database, std::string_view account_id,
                    std::string_view task_id, std::int32_t version) -> Result<VerdictView> {
  try {
    for (const auto& row :
         database(sqlpp::select(db::submissions.verdict)
                      .from(db::submissions)
                      .where(db::submissions.accountId == std::string{account_id} &&
                             db::submissions.taskId == std::string{task_id} &&
                             db::submissions.taskVersion == static_cast<std::int64_t>(version))
                      .order_by(db::submissions.submittedAt.desc())
                      .limit(1))) {
      auto view = VerdictView{};
      if (row.verdict.has_value()) {
        view.is_pending = false;
        // Copied out of the row: the connector's buffers do not outlive the loop.
        view.verdict = std::string{*row.verdict};
      }
      return view;
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return VerdictView{};
}

/// The account, task and version a verdict belongs to, plus the words its fragment is rendered in.
struct VerdictTarget {
  std::string account_id;
  std::string task_id;
  std::int32_t version{0};
  Locale locale;
};

/// Resolves the same target the one-shot fragment route uses, so a stream and a page can never show
/// different accounts, versions or locales.
auto resolve_target(db::connection& database, const drogon::HttpRequestPtr& request,
                    std::string_view task_id) -> Result<VerdictTarget> {
  const auto viewer = task_viewer(database, request->getCookie("cms_session"));
  if (!viewer) {
    return std::unexpected(viewer.error());
  }
  const auto permitted =
      authorize(database, viewer->account_kind, viewer->account_id, Capability::content_read);
  if (!permitted && permitted.error().code != ErrorCode::permission_denied) {
    return std::unexpected(permitted.error());
  }
  const auto version = servable_version(database, task_id, viewer->audience, permitted.has_value());
  if (!version) {
    return std::unexpected(version.error());
  }
  if (!*version) {
    return std::unexpected(Error::make(ErrorCode::not_found, "no such task"));
  }
  return VerdictTarget{viewer->account_id, std::string{task_id}, **version,
                       choose_locale(request->getParameter("lang"),
                                     request->getHeader("accept-language"))};
}

auto refuse(const std::function<void(const drogon::HttpResponsePtr&)>& callback, const Error& error)
    -> void {
  if (error.code == ErrorCode::permission_denied) {
    callback(redirect(SIGN_IN_PATH));
    return;
  }
  if (error.code == ErrorCode::not_found) {
    callback(text_response(std::string{NO_TASK_BODY}, drogon::k404NotFound));
    return;
  }
  callback(text_response(std::string{INTERNAL_ERROR_BODY}, drogon::k500InternalServerError));
}

struct pg_deleter {
  void operator()(PGconn* connection) const { PQfinish(connection); }
};

using pg_connection = std::unique_ptr<PGconn, pg_deleter>;

auto pg_usable(PGconn* connection) -> bool {
  return connection != nullptr && PQstatus(connection) == CONNECTION_OK;
}

auto pg_listen(PGconn* connection) -> bool {
  const auto statement = "LISTEN " + std::string{VERDICT_CHANNEL};
  auto* result = PQexec(connection, statement.c_str());
  if (result == nullptr) {
    return false;
  }
  const auto ok = PQresultStatus(result) == PGRES_COMMAND_OK;
  PQclear(result);
  return ok;
}

auto pg_wait_readable(PGconn* connection, std::chrono::seconds timeout) -> bool {
  const auto socket = PQsocket(connection);
  if (socket < 0) {
    return false;
  }
  auto readable = fd_set{};
  FD_ZERO(&readable);
  FD_SET(socket, &readable);

  auto wait = timeval{};
  wait.tv_sec = static_cast<decltype(wait.tv_sec)>(timeout.count());
  wait.tv_usec = 0;

  const auto ready = ::select(socket + 1, &readable, nullptr, nullptr, &wait);
  return ready > 0 && FD_ISSET(socket, &readable);
}

/// Splits `account:task:version`; a notification that does not fit is ignored rather than guessed at.
auto parse_notification(const std::string& payload)
    -> std::optional<std::tuple<std::string, std::string, std::int32_t>> {
  const auto first = payload.find(':');
  if (first == std::string::npos) {
    return std::nullopt;
  }
  const auto second = payload.find(':', first + 1);
  if (second == std::string::npos || second == first + 1) {
    return std::nullopt;
  }
  try {
    return std::make_tuple(payload.substr(0, first), payload.substr(first + 1, second - first - 1),
                           std::stoi(payload.substr(second + 1)));
  } catch (...) {
    return std::nullopt;
  }
}

/// The open verdict streams and the one listener that feeds them.
///
/// A subscription is keyed by the account, task and version the page rendered; the write's notification
/// carries the same three values, so a change is delivered only to the pages whose row it changed. The
/// listener runs for the process lifetime, because a connection per page would multiply the cost the
/// stream exists to remove.
class VerdictStreams {
 public:
  static auto instance() -> VerdictStreams& {
    static auto streams = VerdictStreams{};
    return streams;
  }

  ~VerdictStreams() {
    running_ = false;
    if (listener_.joinable()) {
      listener_.join();
    }
  }

  VerdictStreams(const VerdictStreams&) = delete;
  VerdictStreams& operator=(const VerdictStreams&) = delete;
  VerdictStreams(VerdictStreams&&) = delete;
  VerdictStreams& operator=(VerdictStreams&&) = delete;

  void start(std::shared_ptr<const db::connection_config> config, std::string dsn) {
    config_ = std::move(config);
    max_streams_ = STREAMS_PER_SESSION;
    seconds_ = STREAM_SECONDS;
    if (dsn.empty() || running_) {
      return;
    }
    dsn_ = std::move(dsn);
    running_ = true;
    listener_ = std::thread([this] { listen(); });
  }

  auto acquire(const std::string& session) -> bool {
    const auto guard = std::lock_guard{mutex_};
    auto& held = sessions_[session];
    if (held >= max_streams_) {
      return false;
    }
    ++held;
    return true;
  }

  void release(const std::string& session) {
    const auto guard = std::lock_guard{mutex_};
    const auto found = sessions_.find(session);
    if (found == sessions_.end()) {
      return;
    }
    if (--found->second == 0) {
      sessions_.erase(found);
    }
  }

  void open(const std::string& session, const VerdictTarget& target,
            drogon::ResponseStreamPtr stream) {
    auto id = std::uint64_t{0};
    {
      const auto guard = std::lock_guard{mutex_};
      id = next_id_++;
      subscribers_.emplace(id, Subscriber{session, target,
                                          std::shared_ptr<drogon::ResponseStream>{std::move(stream)}});
    }
    auto* loop = drogon::app().getLoop();
    loop->runAfter(static_cast<double>(seconds_), [this, id] { close(id); });
    loop->queueInLoop([this, id] { send_current(id); });
  }

  void deliver(const std::string& account_id, const std::string& task_id, std::int32_t version) {
    auto matches = std::vector<std::pair<std::uint64_t, Locale>>{};
    {
      const auto guard = std::lock_guard{mutex_};
      for (const auto& [id, subscriber] : subscribers_) {
        if (subscriber.target.account_id == account_id && subscriber.target.task_id == task_id &&
            subscriber.target.version == version) {
          matches.emplace_back(id, subscriber.target.locale);
        }
      }
    }
    if (matches.empty()) {
      return;
    }
    auto database = db::connection{config_};
    const auto verdict = latest_verdict(database, account_id, task_id, version);
    if (!verdict) {
      return;
    }
    for (const auto& [id, locale] : matches) {
      const auto fragment = render_fragment(
          verdict_fragment_source(verdict->is_pending),
          verdict_fragment_model(locale, *verdict));
      if (fragment) {
        send_event(id, "verdict", *fragment);
      }
    }
  }

 private:
  struct Subscriber {
    std::string session;
    VerdictTarget target;
    std::shared_ptr<drogon::ResponseStream> stream;
  };

  VerdictStreams() = default;

  void send_current(std::uint64_t id) {
    auto target = VerdictTarget{};
    {
      const auto guard = std::lock_guard{mutex_};
      const auto found = subscribers_.find(id);
      if (found == subscribers_.end()) {
        return;
      }
      target = found->second.target;
    }
    auto database = db::connection{config_};
    const auto verdict =
        latest_verdict(database, target.account_id, target.task_id, target.version);
    if (!verdict) {
      return;
    }
    const auto fragment = render_fragment(verdict_fragment_source(verdict->is_pending),
                                          verdict_fragment_model(target.locale, *verdict));
    if (fragment) {
      send_event(id, "verdict", *fragment);
    }
  }

  void send_event(std::uint64_t id, std::string_view name, const std::string& body) {
    auto stream = std::shared_ptr<drogon::ResponseStream>{};
    {
      const auto guard = std::lock_guard{mutex_};
      const auto found = subscribers_.find(id);
      if (found == subscribers_.end()) {
        return;
      }
      stream = found->second.stream;
    }
    auto payload = std::string{"event: "} + std::string{name} + '\n';
    auto start = std::size_t{0};
    while (start <= body.size()) {
      const auto end = body.find('\n', start);
      const auto length = end == std::string::npos ? body.size() - start : end - start;
      payload += "data: " + body.substr(start, length) + '\n';
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
    payload += '\n';
    if (!stream->send(payload)) {
      close(id);
    }
  }

  void close(std::uint64_t id) {
    auto stream = std::shared_ptr<drogon::ResponseStream>{};
    auto session = std::string{};
    {
      const auto guard = std::lock_guard{mutex_};
      const auto found = subscribers_.find(id);
      if (found == subscribers_.end()) {
        return;
      }
      stream = std::move(found->second.stream);
      session = found->second.session;
      subscribers_.erase(found);
    }
    release(session);
    if (stream) {
      stream->close();
    }
  }

  void listen() {
    auto connection = pg_connection{};
    while (running_) {
      if (!pg_usable(connection.get())) {
        connection.reset(PQconnectdb(dsn_.c_str()));
        if (!pg_usable(connection.get()) || !pg_listen(connection.get())) {
          connection.reset();
          std::this_thread::sleep_for(std::chrono::seconds{1});
          continue;
        }
      }
      if (!pg_wait_readable(connection.get(), std::chrono::seconds{1})) {
        continue;
      }
      if (PQconsumeInput(connection.get()) != 1) {
        connection.reset();
        continue;
      }
      while (auto* notification = PQnotifies(connection.get())) {
        const auto payload =
            std::string{notification->extra == nullptr ? "" : notification->extra};
        PQfreemem(notification);
        const auto key = parse_notification(payload);
        if (!key) {
          continue;
        }
        auto [account_id, task_id, version] = *key;
        drogon::app().getLoop()->queueInLoop(
            [this, account_id = std::move(account_id), task_id = std::move(task_id), version] {
              deliver(account_id, task_id, version);
            });
      }
    }
  }

  std::shared_ptr<const db::connection_config> config_;
  std::string dsn_;
  std::mutex mutex_;
  std::unordered_map<std::uint64_t, Subscriber> subscribers_;
  std::unordered_map<std::string, unsigned> sessions_;
  std::uint64_t next_id_{1};
  unsigned max_streams_{STREAMS_PER_SESSION};
  unsigned seconds_{STREAM_SECONDS};
  std::thread listener_;
  std::atomic<bool> running_{false};
};

}  // namespace

auto register_verdict_route(const std::shared_ptr<const db::connection_config>& database_config,
                            const std::string& database_url) -> void {
  drogon::app().registerHandler(
      "/tasks/{}/verdict",
      [database_config](const drogon::HttpRequestPtr& request,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                        std::string_view task_id) {
        auto database = db::connection{database_config};
        const auto target = resolve_target(database, request, task_id);
        if (!target) {
          refuse(callback, target.error());
          return;
        }
        const auto verdict =
            latest_verdict(database, target->account_id, target->task_id, target->version);
        if (!verdict) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto fragment = render_fragment(verdict_fragment_source(verdict->is_pending),
                                              verdict_fragment_model(target->locale, *verdict));
        if (!fragment) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        callback(html_response(*fragment));
      },
      {drogon::Get});

  auto& streams = VerdictStreams::instance();
  streams.start(database_config, database_url);
  drogon::app().registerHandler(
      "/tasks/{}/verdict/stream",
      [database_config, &streams](const drogon::HttpRequestPtr& request,
                                  std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                                  std::string_view task_id) {
        auto database = db::connection{database_config};
        const auto target = resolve_target(database, request, task_id);
        if (!target) {
          refuse(callback, target.error());
          return;
        }
        auto session = std::string{request->getCookie("cms_session")};
        if (!streams.acquire(session)) {
          callback(text_response("too many open verdict streams",
                                 drogon::k429TooManyRequests));
          return;
        }
        auto response = drogon::HttpResponse::newAsyncStreamResponse(
            [&streams, target = *target, session](drogon::ResponseStreamPtr stream) {
              if (!stream) {
                streams.release(session);
                return;
              }
              streams.open(session, target, std::move(stream));
            },
            true);
        response->addHeader("Content-Type", "text/event-stream");
        response->addHeader("Cache-Control", "no-cache");
        response->addHeader("X-Accel-Buffering", "no");
        callback(response);
      },
      {drogon::Get});
}

}  // namespace cms::api
