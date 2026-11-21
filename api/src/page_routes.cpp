#include <cms/api/page_routes.hpp>

#include <algorithm>
#include <charconv>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <drogon/drogon.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/api/auth.hpp>
#include <cms/api/authorize.hpp>
#include <cms/api/form_body.hpp>
#include <cms/api/http.hpp>
#include <cms/api/route_text.hpp>
#include <cms/api/settings_page.hpp>
#include <cms/api/settings_route.hpp>
#include <cms/api/settings_view.hpp>
#include <cms/api/task_view.hpp>
#include <cms/api/uuid.hpp>
#include <cms/api/web.hpp>
#include <cms/bank/bank.hpp>
#include <cms/bank/serve.hpp>
#include <cms/db/tables.hpp>
#include <cms/bank/curriculum.hpp>
#include <cms/capability.hpp>

namespace cms::api {
namespace {

constexpr std::string_view SIGN_IN_PATH = "/signin";
constexpr std::string_view TOPIC_PARAMETER_PREFIX = "topic:";
constexpr char BAND_SEPARATOR = '-';
constexpr std::int32_t BAND_MINIMUM = 800;
constexpr std::int32_t BAND_MAXIMUM = 3500;
constexpr std::int32_t BAND_STEP = 100;

/// A difficulty on the one scale the content gate enforces: 800-3500, step 100.
auto is_band_edge(std::int32_t value) -> bool {
  return value >= BAND_MINIMUM && value <= BAND_MAXIMUM && value % BAND_STEP == 0;
}

/// One whole number and nothing after it: `from_chars` alone stops at the first character it cannot read,
/// so `1500x` would otherwise parse as the band edge `1500`.
auto parse_whole_number(std::string_view text, std::int32_t& number) -> bool {
  const auto parsed = std::from_chars(text.begin(), text.end(), number);
  return parsed.ec == std::errc{} && parsed.ptr == text.end();
}

/// `1500-1800` as the two numbers a filter runs with; nothing when that is not a band of the scale.
auto parse_band(std::string_view text) -> std::optional<std::pair<std::int32_t, std::int32_t>> {
  const auto separator = text.find(BAND_SEPARATOR);
  if (separator == std::string_view::npos) {
    return std::nullopt;
  }
  auto lowest = std::int32_t{0};
  auto highest = std::int32_t{0};
  if (!parse_whole_number(text.substr(0, separator), lowest) ||
      !parse_whole_number(text.substr(separator + 1), highest)) {
    return std::nullopt;
  }
  if (!is_band_edge(lowest) || !is_band_edge(highest) || lowest > highest) {
    return std::nullopt;
  }
  return std::pair{lowest, highest};
}

/// The topics a filter form submitted: one parameter per checked box, named `topic:<id>`. Repeating a query
/// key is not something the request can express, so the id travels in the name rather than in the value.
template <typename Parameters>
auto checked_topics(const Parameters& parameters) -> std::vector<std::string> {
  auto checked = std::vector<std::string>{};
  for (const auto& [key, value] : parameters) {
    if (value.empty() || !key.starts_with(TOPIC_PARAMETER_PREFIX)) {
      continue;
    }
    checked.push_back(key.substr(TOPIC_PARAMETER_PREFIX.size()));
  }
  std::sort(checked.begin(), checked.end());
  return checked;
}

auto find_stage(const std::vector<bank::Track>& tracks, std::string_view track_id,
                std::string_view stage_id) -> const bank::Stage* {
  for (const auto& track : tracks) {
    if (track.id != track_id) {
      continue;
    }
    for (const auto& stage : track.stages) {
      if (stage.id == stage_id) {
        return &stage;
      }
    }
  }
  return nullptr;
}

/// The rung list and the rung this request's learner has chosen.
///
/// One query pair for the vocabulary and one for the account: the meter in the shell and the filter on the
/// all-tasks page both read this, which is what keeps them one vocabulary rather than two copies.
auto level_view(db::connection& database, std::string_view session_id) -> Result<LevelView> {
  auto view = LevelView{};
  // A cookie that is not a uuid cannot name a session, and comparing it with the uuid column would raise
  // instead of finding nothing: the same refusal the submission write makes, for the same reason.
  const auto has_session = has_uuid_shape(session_id);
  try {
    for (const auto& row : database(sqlpp::select(db::levels.id, db::levels.difficultyMin,
                                                  db::levels.difficultyMax)
                                        .from(db::levels)
                                        .order_by(db::levels.position.asc()))) {
      view.rungs.push_back(LevelRung{.id = std::string{row.id},
                                     .title_en = {},
                                     .title_th = {},
                                     .difficulty_min = static_cast<std::int32_t>(row.difficultyMin),
                                     .difficulty_max = static_cast<std::int32_t>(row.difficultyMax)});
    }
    for (const auto& row : database(sqlpp::select(db::levelTitles.levelId, db::levelTitles.language,
                                                  db::levelTitles.title)
                                        .from(db::levelTitles))) {
      const auto language = std::string{row.language};
      for (auto& rung : view.rungs) {
        if (rung.id != std::string_view{row.levelId}) {
          continue;
        }
        if (language == "en") {
          rung.title_en = std::string{row.title};
        } else if (language == "th") {
          rung.title_th = std::string{row.title};
        }
      }
    }

    if (!has_session) {
      return view;
    }
    const auto account = account_of_session(database, session_id);
    if (!account) {
      return std::unexpected(account.error());
    }
    if (account->empty() || account->kind != "learner") {
      return view;
    }
    view.is_learner = true;
    for (const auto& row :
         database(sqlpp::select(db::learnerAccounts.levelId)
                      .from(db::learnerAccounts)
                      .where(db::learnerAccounts.id == account->id))) {
      if (row.levelId.has_value()) {
        view.current_id = std::string{*row.levelId};
      }
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return view;
}

}  // namespace

/// The title a task is shown by, with the same fallback every page uses: the reader's locale, else the other.
auto localized_title(std::string_view locale_code, std::string_view english, std::string_view thai)
    -> std::string {
  const auto wants_thai = locale_code != "en";
  const auto preferred = wants_thai ? thai : english;
  const auto fallback = wants_thai ? english : thai;
  return std::string{preferred.empty() ? fallback : preferred};
}

/// What the home route read: whether the request carries a learner, and one entry per stage when it does.
struct PathState {
  std::vector<StagePath> paths;
  bool is_learner{false};
};

/// Reads the path state the page shows beside each stage: how much of it the learner has done, and the one
/// task the bank's own rule offers next. A request with no learner reads nothing, because there is nobody
/// to choose for.
auto read_path_state(db::connection& database, const std::vector<bank::Track>& tracks,
                     std::string_view session_id, std::string_view locale_code) -> Result<PathState> {
  auto state = PathState{};
  const auto account = account_of_session(database, session_id);
  if (!account) {
    return std::unexpected(account.error());
  }
  if (account->empty() || account->kind != "learner") {
    return state;
  }
  state.is_learner = true;
  auto& paths = state.paths;
  const auto viewer = task_viewer(database, session_id);
  if (!viewer) {
    return std::unexpected(viewer.error());
  }

  try {
    for (const auto& track : tracks) {
      for (const auto& stage : track.stages) {
        auto path = StagePath{.track_id = track.id,
                              .stage_id = stage.id,
                              .done = 0,
                              .total = 0,
                              .next_id = {},
                              .next_title = {},
                              .has_next = false};
        const auto summary = bank::stage_progress(database, account->id, stage, viewer->audience);
        if (!summary) {
          return std::unexpected(summary.error());
        }
        path.done = summary->done;
        path.total = summary->total;

        // The stage's offer is its own band: a finished stage that reached into the next band would read
        // "done 2 of 2" beside a task it does not contain.
        const auto next =
            bank::next_unseen_task_in_band(database, account->id, stage, viewer->audience);
        if (!next) {
          return std::unexpected(next.error());
        }
        if (stage_offers(*next, summary->done, summary->total)) {
          path.has_next = true;
          path.next_id = (*next)->id;
          path.next_title = (*next)->id;
          for (const auto& row :
               database(sqlpp::select(db::tasks.titleEn, db::tasks.titleTh)
                            .from(db::tasks)
                            .where(db::tasks.id == (*next)->id &&
                                   db::tasks.version ==
                                       static_cast<std::int64_t>((*next)->version)))) {
            path.next_title = localized_title(locale_code, row.titleEn, row.titleTh);
          }
        }
        paths.push_back(std::move(path));
      }
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return state;
}

auto register_home_route(const std::shared_ptr<SettingsStore>& store,
                         const std::shared_ptr<const db::connection_config>& database_config,
                         std::string_view template_root) -> void {
  drogon::app().registerHandler(
      "/",
      [store, database_config, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto curriculum = bank::read_curriculum(database);
        if (!curriculum) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        const auto levels = level_view(database, request->getCookie("cms_session"));
        if (!levels) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto path_state = read_path_state(database, *curriculum,
                                                request->getCookie("cms_session"), locale.code);
        if (!path_state) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto page = render_page(
            template_root, "home.html",
            home_page_model(locale, *curriculum, path_state->paths, site_name_of(*store), *levels,
                            path_state->is_learner));
        if (!page) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        callback(html_response(*page));
      },
      {drogon::Get});
}

auto register_task_route(const std::shared_ptr<SettingsStore>& store,
                         const std::shared_ptr<const db::connection_config>& database_config,
                         std::string_view template_root) -> void {
  drogon::app().registerHandler(
      "/tasks/{}",
      [store, database_config, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback,
          std::string_view task_id) {
        auto database = db::connection{database_config};
        const auto viewer = task_viewer(database, request->getCookie("cms_session"));
        if (!viewer) {
          if (viewer.error().code == ErrorCode::permission_denied) {
            callback(redirect(SIGN_IN_PATH));
          } else {
            callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                   drogon::k500InternalServerError));
          }
          return;
        }
        const auto permitted = authorize(database, viewer->account_kind, viewer->account_id, Capability::content_read);
        if (!permitted && permitted.error().code != ErrorCode::permission_denied) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto task =
            read_task_page(database, task_id, viewer->audience, permitted.has_value());
        if (!task) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        if (!*task) {
          // A row that exists but carries no verified certificate is told apart from an unknown id, because the reader
          // asking is the one who would publish it and the difference is the whole reason they cannot.
          callback(text_response(std::string{read_task_refusal(database, task_id, permitted.has_value())},
                                 drogon::k404NotFound));
          return;
        }

        const auto levels = level_view(database, request->getCookie("cms_session"));
        if (!levels) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto page = render_page(
            template_root, "task.html",
            task_model(locale, **task, site_name_of(*store), *levels));
        if (!page) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        callback(html_response(*page));
      },
      {drogon::Get});
}

/// Serves the settings page. An unauthenticated request is redirected to the sign-in page because it is not
/// forbidden, it is unauthenticated; a session without `settings_read` is forbidden. The values come from the
/// cached visible list, so a secret is redacted before this route ever sees it.
auto register_settings_route(const std::shared_ptr<SettingsStore>& store,
                             const std::shared_ptr<const db::connection_config>& database_config,
                             const std::shared_ptr<const SettingsView>& view,
                             std::string_view template_root) -> void {
  drogon::app().registerHandler(
      "/settings",
      [store, database_config, view, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto principal = account_of_session(database, request->getCookie("cms_session"));
        if (!principal) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        if (principal->empty()) {
          callback(redirect(SIGN_IN_PATH));
          return;
        }
        const auto permitted =
            authorize(database, principal->kind, principal->id, Capability::settings_read);
        if (!permitted) {
          if (permitted.error().code != ErrorCode::permission_denied) {
            callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                   drogon::k500InternalServerError));
          } else {
            callback(text_response(permitted.error().message, drogon::k403Forbidden));
          }
          return;
        }

        const auto snapshot = store->load();
        if (snapshot == nullptr) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto page = render_page(
            template_root, "settings.html",
            settings_model(locale, site_name_of(*store), *view->for_snapshot(*snapshot),
                           request->getParameter("group")));
        if (!page) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        callback(html_response(*page));
      },
      {drogon::Get});
}


auto register_task_list_route(const std::shared_ptr<SettingsStore>& store,
                              const std::shared_ptr<const db::connection_config>& database_config,
                              std::string_view template_root) -> void {
  drogon::app().registerHandler(
      "/tasks",
      [store, database_config, template_root = std::string{template_root}](
          const drogon::HttpRequestPtr& request,
          std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto viewer = task_viewer(database, request->getCookie("cms_session"));
        if (!viewer) {
          if (viewer.error().code == ErrorCode::permission_denied) {
            callback(redirect(SIGN_IN_PATH));
          } else {
            callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                   drogon::k500InternalServerError));
          }
          return;
        }
        const auto permitted = authorize(database, viewer->account_kind, viewer->account_id,
                                         Capability::content_read);
        if (!permitted && permitted.error().code != ErrorCode::permission_denied) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }

        auto page = TaskListPage{};
        auto filters = TaskListFilters{};

        const auto level = request->getParameter("level");
        if (!level.empty()) {
          const auto band = parse_band(level);
          if (!band) {
            callback(text_response("the level filter is not a difficulty band on the 800-3500 scale",
                                   drogon::k400BadRequest));
            return;
          }
          page.level_min = band->first;
          page.level_max = band->second;
          filters.level_min = band->first;
          filters.level_max = band->second;
        }

        const auto track_id = request->getParameter("track");
        const auto stage_id = request->getParameter("stage");
        if (!track_id.empty() || !stage_id.empty()) {
          if (track_id.empty() || stage_id.empty()) {
            callback(text_response("a stage is named by both its track and its id",
                                   drogon::k400BadRequest));
            return;
          }
          const auto curriculum = bank::read_curriculum(database);
          if (!curriculum) {
            callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                   drogon::k500InternalServerError));
            return;
          }
          const auto* stage = find_stage(*curriculum, track_id, stage_id);
          if (stage == nullptr) {
            callback(text_response("no stage " + stage_id + " in track " + track_id,
                                   drogon::k400BadRequest));
            return;
          }
          page.stage_track = track_id;
          page.stage_id = stage_id;
          page.stage_title_en = stage->title_en;
          page.stage_title_th = stage->title_th;
          filters.stage_min = stage->difficulty_min;
          filters.stage_max = stage->difficulty_max;
          filters.stage_topics = stage->tags;
        }

        const auto vocabulary = read_topic_vocabulary(database);
        if (!vocabulary) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto selected = checked_topics(request->getParameters());
        for (const auto& topic_id : selected) {
          const auto known = std::any_of(
              vocabulary->begin(), vocabulary->end(),
              [&topic_id](const bank::Topic& topic) { return topic.id == topic_id; });
          if (!known) {
            callback(text_response("no topic called " + topic_id, drogon::k400BadRequest));
            return;
          }
        }
        page.topics = selected;
        filters.topics = selected;

        const auto listed =
            read_task_list(database, filters, viewer->audience, permitted.has_value());
        if (!listed) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        page.tasks = *listed;

        const auto levels = level_view(database, request->getCookie("cms_session"));
        if (!levels) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto locale = choose_locale(request->getParameter("lang"),
                                          request->getHeader("accept-language"));
        const auto rendered = render_page(
            template_root, "tasks.html",
            task_list_model(locale, page, *vocabulary, site_name_of(*store), *levels));
        if (!rendered) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        callback(html_response(*rendered));
      },
      {drogon::Get});
}

auto register_level_route(const std::shared_ptr<const db::connection_config>& database_config)
    -> void {
  drogon::app().registerHandler(
      "/level",
      [database_config](const drogon::HttpRequestPtr& request,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto database = db::connection{database_config};
        const auto session_id = request->getCookie("cms_session");
        if (!has_uuid_shape(session_id)) {
          callback(redirect(SIGN_IN_PATH));
          return;
        }
        const auto account = account_of_session(database, session_id);
        if (!account) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        if (account->empty()) {
          callback(redirect(SIGN_IN_PATH));
          return;
        }
        // Only a learner works at a rung; an administrator has no learning path, and quietly writing an
        // account field nobody reads would hide that the form was shown to the wrong surface.
        if (account->kind != "learner") {
          callback(text_response("only a learner account carries a rung\n", drogon::k403Forbidden));
          return;
        }
        const auto wanted = form_field(request->body(), "level");
        if (wanted.empty()) {
          callback(text_response("a rung is required\n", drogon::k400BadRequest));
          return;
        }
        const auto levels = level_view(database, request->getCookie("cms_session"));
        if (!levels) {
          callback(text_response(std::string{INTERNAL_ERROR_BODY},
                                 drogon::k500InternalServerError));
          return;
        }
        const auto known = std::any_of(
            levels->rungs.begin(), levels->rungs.end(),
            [&wanted](const LevelRung& rung) { return rung.id == wanted; });
        if (!known) {
          callback(text_response("no level called " + wanted + "\n", drogon::k400BadRequest));
          return;
        }
        database(sqlpp::update(db::learnerAccounts)
                     .set(db::learnerAccounts.levelId = wanted)
                     .where(db::learnerAccounts.id == account->id));
        // The list is where the choice shows: it is the page the filter and the meter share.
        callback(redirect("/tasks"));
      },
      {drogon::Post});
}

}  // namespace cms::api
