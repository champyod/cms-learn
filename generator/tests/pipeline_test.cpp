#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/generate/candidates.hpp>
#include <cms/generate/certify.hpp>
#include <cms/generate/model.hpp>
#include <cms/seal.hpp>
#include <cms/generate/pipeline.hpp>
#include <cms/generate/solution.hpp>
#include <cms/generate/specification.hpp>
#include <cms/generate/statement.hpp>
#include <cms/generate/test_plan.hpp>

#include <cms/testing/scratch_directory.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"
#include "pipeline_fixture.hpp"

namespace {

constexpr auto RUN_TASK = "pipeline-run-probe";
constexpr auto BINARY_PATH_TASK = "pipeline-binary-path-probe";
constexpr auto DISABLED_TASK = "pipeline-disabled-model-probe";
constexpr auto REFERENCE_TASK = "pipeline-reference-probe";
constexpr auto REFUSAL_TASK = "pipeline-refusal-probe";
constexpr auto MISSING_TASK = "pipeline-missing-probe";
constexpr auto CERTIFICATE_TASK = "pipeline-certificate-probe";
constexpr auto LOCALE_TASK = "pipeline-locale-probe";
constexpr auto BRIEF_TASK = "pipeline-brief-probe";
constexpr auto WORKER = "worker-probe";
constexpr std::string_view COMMIT = "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678";
/// The task's own limits, equal to what step 3 derives from O(n log n) at the task's bound: the certifier refuses a
/// specification whose derived limits are not the task's, so the fixture states them where the task row carries them.
/// The task's declared limits, which step 3 derives and the certifier's half-limit margin is measured against (ADR 0048).
///
/// The margin is half of these — 556 ms and 38 MB — against a reference that measures about 17 ms and 5 MB: a factor of
/// thirty-two in time and eight in memory. The fixture used to declare 100 ms and 16 MB, a 50 ms margin; a 17 ms
/// reference under a gate running several test binaries at once can cross that, the certificate refuses, and the case
/// fails on a machine that was merely busy. Time is what load moves, so the plan's bound is raised to five million
/// ($specification_answer) until step 3 derives 1113 ms for it, and the declared limits follow step 3 exactly — the
/// certifier refuses a task whose limits disagree with the derived ones, which is what keeps these two in step.
constexpr std::int64_t TASK_TIME_MS = 1113;
constexpr std::int64_t TASK_MEMORY_USED_MB = 77;
constexpr std::int64_t TASK_MEMORY_RESERVED_MB = 154;

constexpr std::string_view SUM_ORACLE = R"(#include <iostream>
int main() {
  long long n = 0;
  std::cin >> n;
  long long total = 0;
  for (long long index = 0; index < n; ++index) {
    long long value = 0;
    std::cin >> value;
    total += value;
  }
  std::cout << total << "\n";
  return 0;
}
)";

constexpr std::string_view SUM_REFERENCE = R"(#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  std::printf("%lld\n", total);
  return 0;
}
)";

/// The reference that is right only on the smallest case: the differential finds it at the subtask's bound.
constexpr std::string_view WRONG_REFERENCE = R"(#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  std::printf("%lld\n", count == 1 ? total : 42LL);
  return 0;
}
)";

/// The first subtask's solution: right up to its own bound, wrong above it, which is where the second subtask lives.
constexpr std::string_view FIRST_PARTIAL = R"(#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  std::printf("%lld\n", count <= 100 ? total : 42LL);
  return 0;
}
)";

/// The second subtask's solution: right from its own floor up, wrong below it, which is where the first subtask lives.
constexpr std::string_view SECOND_PARTIAL = R"(#include <cstdio>
int main() {
  int count = 0;
  if (std::scanf("%d", &count) != 1) return 1;
  long long total = 0;
  for (int index = 0; index < count; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    total = total + value;
  }
  std::printf("%lld\n", count >= 1000 ? total : 42LL);
  return 0;
}
)";

/// A generator implementing the contract: stdin "<seed> <n>", stdout "n" then n values in the declared bounds.
constexpr std::string_view GENERATOR = R"(#include <cstdio>
int main() {
  long long seed = 0;
  long long n = 0;
  if (std::scanf("%lld %lld", &seed, &n) != 2) return 1;
  std::printf("%lld\n", n);
  for (long long index = 0; index < n; ++index) {
    std::printf("0%c", index + 1 == n ? '\n' : ' ');
  }
  return 0;
}
)";

/// A checker implementing the contract: argv expected, answer; exit 0 when the tokens agree.
constexpr std::string_view CHECKER = R"(#include <fstream>
#include <string>
int main(int argc, char** argv) {
  if (argc < 3) return 2;
  std::ifstream expected_file(argv[1]);
  std::ifstream answer_file(argv[2]);
  std::string expected;
  std::string answer;
  while (expected_file >> expected && answer_file >> answer) {
    if (expected != answer) return 1;
  }
  if (expected_file >> expected) return 1;
  if (answer_file >> answer) return 1;
  return 0;
}
)";

/// A validator implementing the contract: stdin is a case, exit 0 when it is inside the declared bounds. It must
/// accept what the generator prints and refuse an empty case, a short one and a value over the bound, because those
/// are the mutations the certification re-runs it against.
constexpr std::string_view VALIDATOR = R"(#include <cstdio>
int main() {
  long long n = 0;
  if (std::scanf("%lld", &n) != 1) return 1;
  if (n < 1 || n > 5000000) return 1;
  for (long long index = 0; index < n; ++index) {
    long long value = 0;
    if (std::scanf("%lld", &value) != 1) return 1;
    if (value < -1000000000 || value > 1000000000) return 1;
  }
  long long extra = 0;
  if (std::scanf("%lld", &extra) == 1) return 1;
  return 0;
}
)";

/// @return @p text as the body of a JSON string, so a program's newlines do not break the answer.
auto json_string(std::string_view text) -> std::string {
  auto out = std::string{"\""};
  for (const auto character : text) {
    if (character == '\n') {
      out += "\\n";
      continue;
    }
    if (character == '"' || character == '\\') {
      out.push_back('\\');
    }
    out.push_back(character);
  }
  out.push_back('"');
  return out;
}

/// @return One shard's answer: the three facts step 2 reads, and the trap sentence ADR 0036 §3 makes the plan carry.
auto planner_answer(std::string_view approach) -> std::string {
  return "{\"approach\": \"" + std::string{approach} +
         "\", \"intended_complexity\": \"O(n log n)\", \"fits_bounds\": true, "
         "\"trap\": \"the reader is meant to believe a scan of every range is fast enough\", "
         "\"constraints\": {\"n\": 200000}}";
}

auto judge_answer() -> std::string {
  return "{\"chosen\": 2, \"reason\": \"the cheapest idea that fits\", \"rejected\": ["
         "{\"position\": 1, \"reason\": \"its cost does not fit the bounds\"}, "
         "{\"position\": 3, \"reason\": \"more machinery than the task needs\"}, "
         "{\"position\": 4, \"reason\": \"a worse constant, no simpler proof\"}]}";
}

auto specification_answer() -> std::string {
  // The bound is step 3's own input: it derives the limits from the complexity and the largest n, so a larger bound is
  // the honest way to ask for a wider half-limit margin (see TASK_TIME_MS).
  return R"({"input": "n, then n integers", "output": "their sum", "input_shape": "n_then_values",)"
         R"("bounds": {"min_n": 1, "max_n": 5000000, "value_min": -1000000000, "value_max": 1000000000},)"
         R"("subtasks": [{"position": 1, "max_n": 100}, )"
         R"({"position": 2, "min_n": 1000, "max_n": 200000}], "oracle": )" +
         json_string(SUM_ORACLE) + "}";
}

auto statement_answer(std::string_view locale) -> std::string {
  auto json = std::string{"{\"title\": \"Range Sum Queries\""};
  for (const auto key : {"task", "input", "output", "constraints", "example", "scoring"}) {
    json += ", ";
    json += json_string(key);
    json += ": ";
    json += json_string(std::string{"The statement's "} + key + " section, written for " +
                        std::string{locale} + ".");
  }
  json += '}';
  return json;
}

/// @return A recording of one whole job: four shards, the decision, step 3, one statement, step 5's two programs, and
/// step 6's two programs.
/// The twelve answers a whole job asks for, in order. Kept as text so a socket can serve exactly what the recorded client
/// answers: the model's shape is the product's, and only where it comes from differs.
auto whole_job_answers(std::string_view reference = SUM_REFERENCE) -> std::vector<std::string> {
  return {planner_answer("prefix sums"), planner_answer("segment tree"), planner_answer("sqrt decomposition"),
          planner_answer("sparse table"), judge_answer(), specification_answer(), statement_answer("en"),
          std::string{reference}, std::string{FIRST_PARTIAL}, std::string{SECOND_PARTIAL}, std::string{GENERATOR},
          std::string{CHECKER}, std::string{VALIDATOR}};
}

auto whole_job_recording(std::string_view reference = SUM_REFERENCE) -> cms::generate::RecordedClient {
  return cms::generate::RecordedClient{"deployer-model", "prompt-v1", 7, whole_job_answers(reference)};
}

/// @brief A model endpoint on this host that answers the OpenAI chat shape, one scripted answer per request.
///
/// The test's model client is the product's adapter, not a stub: it opens a socket, sends the request the adapter builds
/// and parses the answer the same way. Only the vendor is absent, which is the one thing a test cannot stand in for.
class LocalModelServer {
 public:
  explicit LocalModelServer(std::vector<std::string> answers) : answers_(std::move(answers)) {
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(listener_ >= 0);
    auto address = ::sockaddr_in{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    REQUIRE(::bind(listener_, reinterpret_cast<::sockaddr*>(&address), sizeof(address)) == 0);
    auto bound = ::socklen_t{sizeof(address)};
    REQUIRE(::getsockname(listener_, reinterpret_cast<::sockaddr*>(&address), &bound) == 0);
    port_ = ::ntohs(address.sin_port);
    REQUIRE(::listen(listener_, 8) == 0);
    thread_ = std::thread([this] { serve(); });
  }

  ~LocalModelServer() {
    stopping_ = true;
    if (listener_ >= 0) {
      ::shutdown(listener_, SHUT_RDWR);
      ::close(listener_);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  LocalModelServer(const LocalModelServer&) = delete;
  LocalModelServer& operator=(const LocalModelServer&) = delete;

  [[nodiscard]] auto base_url() const -> std::string {
    return "http://127.0.0.1:" + std::to_string(port_) + "/v1";
  }

  [[nodiscard]] auto requests() const -> std::size_t { return answered_; }

 private:
  void serve() {
    while (!stopping_) {
      const auto connection = ::accept(listener_, nullptr, nullptr);
      if (connection < 0) {
        return;
      }
      auto request = std::string{};
      auto buffer = std::array<char, 4096>{};
      while (request.find("\r\n\r\n") == std::string::npos) {
        const auto read = ::read(connection, buffer.data(), buffer.size());
        if (read <= 0) {
          break;
        }
        request.append(buffer.data(), static_cast<std::size_t>(read));
      }
      // The body is scripted by size, not parsed: one request carries one answer.
      const auto header_end = request.find("\r\n\r\n");
      auto length = std::size_t{0};
      if (const auto marker = request.find("Content-Length: "); marker != std::string::npos) {
        length = static_cast<std::size_t>(std::stoul(request.substr(marker + 16)));
      }
      while (header_end != std::string::npos && request.size() < header_end + 4 + length) {
        const auto read = ::read(connection, buffer.data(), buffer.size());
        if (read <= 0) {
          break;
        }
        request.append(buffer.data(), static_cast<std::size_t>(read));
      }
      const auto answer = answers_.at(answered_ < answers_.size() ? answered_ : answers_.size() - 1);
      ++answered_;
      // A JSON string may not hold a raw control character, and the answers carry newlines and tabs.
      auto escaped = std::string{};
      for (const auto character : answer) {
        if (character == '"' || character == '\\') {
          escaped += '\\';
          escaped += character;
        } else if (static_cast<unsigned char>(character) < 0x20) {
          const auto digits = "0123456789abcdef";
          const auto code = static_cast<unsigned int>(static_cast<unsigned char>(character));
          escaped += "\\u00";
          escaped += digits[code / 16U];
          escaped += digits[code % 16U];
        } else {
          escaped += character;
        }
      }
      const auto body = "{\"choices\":[{\"message\":{\"content\":\"" + escaped + "\"}}]}";
      const auto response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
      (void)::write(connection, response.data(), response.size());
      ::close(connection);
    }
  }

  int listener_{-1};
  std::uint16_t port_{0};
  std::vector<std::string> answers_;
  std::size_t answered_{0};
  std::atomic<bool> stopping_{false};
  std::thread thread_;
};

/// The deployment's seal key in this binary, and the helper the model fixtures seal with.
constexpr std::string_view TEST_SEAL_KEY = "0123456789abcdef0123456789abcdef";

auto sealed(std::string_view plaintext) -> std::string {
  const auto sealer = cms::seal::Sealer::from_key(TEST_SEAL_KEY);
  REQUIRE(sealer.has_value());
  const auto value = sealer->seal(plaintext);
  REQUIRE(value.has_value());
  return *value;
}

/// @brief The model record the operator added: sealed key, protocol, endpoint, one default.
auto write_model_record(cms::db::connection& database, std::string_view name, std::string_view base_url,
                        std::string_view model_id, std::string_view sealed_key, bool enabled = true,
                        bool is_default = true) -> void {
  // One transaction, so the actor the stamp trigger reads is set on the same connection the insert runs on: the pool
  // hands a different connection per statement otherwise.
  database.execute_text("INSERT INTO system_accounts (id, handle) VALUES "
                        "('00000000-0000-4000-8000-000000000033', 'system:pipeline-model') ON CONFLICT DO NOTHING",
                        {});
  auto transaction = cms::db::Transaction{*cms::generate::test::live_client()};
  auto& connection = transaction.connection();
  connection.execute_text("SET LOCAL cms.actor_kind = 'system'", {});
  connection.execute_text("SET LOCAL cms.actor_id = '00000000-0000-4000-8000-000000000033'", {});
  connection.execute_text("DELETE FROM models", {});
  connection.execute_text("INSERT INTO models (name, base_url, protocol, model_id, sealed_key, enabled, is_default, "
                          "actor_kind) VALUES ($1, $2, 'openai_chat', $3, $4, $5, $6, 'system')",
                          {std::string{name}, std::string{base_url}, std::string{model_id}, std::string{sealed_key},
                           enabled, is_default});
  transaction.commit();
}

SQLPP_CREATE_NAME_TAG(certificate_rows);
SQLPP_CREATE_NAME_TAG(certificate_checks);

using certificate_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<certificate_rows_t::_sqlpp_name_tag,
                                                                   ::sqlpp::integral>>;

/// @return The checks the job's newest certificate recorded, so a failed whole run can say which margin it broke.
auto checks_of(cms::db::connection& database, std::int64_t job_id) -> std::vector<std::string> {
  auto checks = std::vector<std::string>{};
  using checks_row = ::sqlpp::result_row_t<::sqlpp::field_spec_t<certificate_checks_t::_sqlpp_name_tag, ::sqlpp::text>>;
  auto rows = ::sqlpp::result_t<cms::db::result, checks_row>{database.query_text(
      "SELECT checks::text AS certificate_checks FROM generation_certificates WHERE job_id = $1 "
      "ORDER BY id DESC LIMIT 1", {job_id})};
  for (const auto& row : rows) {
    checks.emplace_back(row.certificate_checks);
  }
  return checks;
}

auto certificates_of(cms::db::connection& database, std::int64_t job_id) -> std::int64_t {
  auto rows = ::sqlpp::result_t<cms::db::result, certificate_row>{database.query_text(
      "SELECT count(*) AS certificate_rows FROM generation_certificates WHERE job_id = $1", {job_id})};
  return rows.empty() ? 0 : rows.front().certificate_rows;
}


/// @return The model id the job's newest artefact records, which is the record the call went through.
auto last_artefact_model_id(cms::db::connection& database, std::int64_t job_id) -> std::string {
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.modelId)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job_id)
                                      .order_by(cms::db::generationArtefacts.id.desc())
                                      .limit(1))) {
    return std::string{row.modelId};
  }
  return {};
}

/// @brief A model record the operator has disabled: the worker must refuse by name rather than call it.
auto write_model_record_clear(cms::db::connection& database) -> void { database.execute_text("DELETE FROM models", {}); }

/// @brief A certifier that answers what a certificate would say, without running the three methods again.
///
/// The certifier's own verdicts are certify_test's evidence: its memory method measures the child's peak RSS, which
/// under a sanitizer-built test binary includes the pages the child inherits from this process, so a small derived
/// memory limit is measured against the harness rather than against the program. The runner's rule is what this file
/// proves: the ending follows the certificate, whichever way it reads.
class StubCertifier final : public cms::generate::Certifier {
 public:
  StubCertifier(std::string_view task_id, cms::generate::VerificationOutcome outcome,
                std::string check = {})
      : task_id_(task_id), outcome_(outcome), check_(std::move(check)) {}

  [[nodiscard]] auto certify(std::int64_t job_id) -> cms::Result<cms::generate::Certificate> override {
    auto checks = std::vector<std::string>{"the evidence was read back through its digests"};
    if (!check_.empty()) {
      checks.push_back(check_);
    }
    return cms::generate::Certificate{.job_id = job_id,
                                      .task_id = std::string{task_id_},
                                      .task_version = 1,
                                      .outcome = outcome_,
                                      .commit_id = std::string{COMMIT},
                                      .checks = std::move(checks),
                                      .artefacts = {}};
  }

 private:
  std::string task_id_;
  cms::generate::VerificationOutcome outcome_;
  std::string check_;
};

/// @brief Leaves the case's job the only claimable one.
///
/// The live cases share one database and a run claims the oldest queued job, so the jobs earlier cases left queued
/// are ended first — the same way a worker that finished them would — or this case's run would take one of theirs.
auto drain_queue(cms::db::connection& database) -> void {
  database.execute_text("UPDATE generation_jobs SET state = 'failed' WHERE state = 'queued'", {});
}

/// @brief Sets the actor the audit triggers read, the way every live fixture that writes content must.
auto actor(cms::db::connection& database) -> void {
  database.execute_text(
      "INSERT INTO admin_accounts (id, handle, status) "
      "VALUES ('a0000001-1111-4111-8111-0000000000c2'::uuid, 'pipeline_probe', 'active') "
      "ON CONFLICT (id) DO NOTHING",
      {});
  database.execute_text("SELECT set_config('cms.actor_id', $1, true)",
                        {std::string{"a0000001-1111-4111-8111-0000000000c2"}});
  database.execute_text("SELECT set_config('cms.actor_kind', 'admin', true)", {});
  database.execute_text("SELECT set_config('cms.invoked_by', 'pipeline_test', true)", {});
}

/// @brief Writes one draft task with the limits step 3 derives, its topic, statement and subtasks, and one queued job.
///
/// Everything that writes content runs in one transaction with this fixture's actor, because the audit triggers read
/// that setting only for the transaction that carries it.
/// @return The job's id.
auto seed_job(std::string_view task_id) -> std::int64_t {
  auto transaction = cms::db::Transaction{*cms::generate::test::live_client()};
  auto& connection = transaction.connection();
  actor(connection);
  const auto job = cms::generate::test::prepare_job(connection, task_id);
  connection.execute_text(
      "INSERT INTO topics (id, label_en, label_th) VALUES ($1, $2, $3) ON CONFLICT (id) DO NOTHING",
      {"prefix-sums", "Prefix sums", "ผลรวมสะสม"});
  connection.execute_text(
      "UPDATE tasks SET time_limit_ms = $3, memory_used_limit_mb = $4, memory_reserved_limit_mb = $5 "
      "WHERE id = $1 AND version = $2",
      {std::string{task_id}, std::int64_t{1}, TASK_TIME_MS, TASK_MEMORY_USED_MB, TASK_MEMORY_RESERVED_MB});
  // The teacher's request lives on the job row (ADR 0036 §2); step 4 refuses a job that stored none.
  connection.execute_text("UPDATE generation_jobs SET brief = $2, description = $3 WHERE id = $1",
                          {job, "a sequence and its range sums", "range sums probe"});
  connection.execute_text("INSERT INTO task_topics (task_id, task_version, topic) VALUES ($1, 1, $2)",
                          {std::string{task_id}, "prefix-sums"});
  // A task carries subtasks only when its type scores by subtask, so the fixture declares that mode the way the seeder
  // does: one type, the field definitions it names, and the value the form would record.
  connection.execute_text(
      "INSERT INTO task_types (id, title_en, title_th, position) VALUES ('batch', 'Batch', 'ชุด', 1) "
      "ON CONFLICT (id) DO NOTHING",
      {});
  connection.execute_text(
      "INSERT INTO task_field_defs (task_type_id, key, kind, label_en, label_th, required, choices, position) "
      "VALUES ('batch', 'score_mode', 'choice', 'Score mode', 'วิธีให้คะแนน', true, "
      "'[\"per_case\", \"all_or_nothing\", \"subtasks\"]'::jsonb, 1) ON CONFLICT DO NOTHING",
      {});
  connection.execute_text("UPDATE tasks SET task_type = 'batch' WHERE id = $1 AND version = 1",
                          {std::string{task_id}});
  connection.execute_text(
      "INSERT INTO task_field_values (task_id, task_version, field_key, value) "
      "VALUES ($1, 1, 'score_mode', '\"subtasks\"'::jsonb)",
      {std::string{task_id}});
  // The task's own subtasks, which step 3's specification narrows and the certifier compares the solutions against.
  connection.execute_text(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, max_n) VALUES ($1, 1, 1, 40, 100)",
      {std::string{task_id}});
  connection.execute_text(
      "INSERT INTO task_subtasks (task_id, task_version, position, points, min_n, max_n) "
      "VALUES ($1, 1, 2, 60, 1000, 200000)",
      {std::string{task_id}});
  connection.execute_text(
      "INSERT INTO task_statements (task_id, task_version, locale, body) VALUES ($1, 1, $2, $3)",
      {std::string{task_id}, "en", "# Probe\n\nBody."});
  transaction.commit();
  return job;
}

/// @return The job's state, holder and attempt count as the record holds them.
auto job_row(cms::db::connection& database, std::int64_t job_id)
    -> std::tuple<std::string, std::string, std::int64_t> {
  auto state = std::string{};
  auto holder = std::string{};
  auto attempts = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.state, cms::db::generationJobs.claimedBy,
                                                cms::db::generationJobs.attempts)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.id == job_id))) {
    state = std::string{row.state};
    holder = row.claimedBy.has_value() ? std::string{*row.claimedBy} : std::string{};
    attempts = static_cast<std::int64_t>(row.attempts);
  }
  return {state, holder, attempts};
}

/// @return The task's verification, which only the certifier's certificate may move.
auto verification_of(cms::db::connection& database, std::string_view task_id) -> std::string {
  auto verification = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::tasks.verification)
                                      .from(cms::db::tasks)
                                      .where(cms::db::tasks.id == std::string{task_id}))) {
    verification = std::string{row.verification};
  }
  return verification;
}

/// @return The refusal rows the job's record holds, as (step, text).
auto refusals_of(cms::db::connection& database, std::int64_t job_id)
    -> std::vector<std::pair<std::int64_t, std::string>> {
  auto rows = std::vector<std::pair<std::int64_t, std::string>>{};
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.kind, cms::db::generationArtefacts.step,
                                                cms::db::generationArtefacts.inlineText)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job_id))) {
    if (row.kind == "refusal") {
      rows.emplace_back(static_cast<std::int64_t>(row.step), std::string{*row.inlineText});
    }
  }
  return rows;
}

/// @brief The sinks and the certifier one run writes through, as the binary builds them.
struct Wired {
  Wired(cms::generate::Certifier& certifier, cms::generate::test::MemoryStore& store)
      : candidates{*cms::generate::test::live_client()},
        specifications{*cms::generate::test::live_client(), store},
        artefacts{*cms::generate::test::live_client(), store},
        statements{*cms::generate::test::live_client(), store},
        solutions{*cms::generate::test::live_client(), store},
        test_plans{*cms::generate::test::live_client(), store},
        ports{.client = *cms::generate::test::live_client(),
              .store = store,
              .candidates = candidates,
              .specifications = specifications,
              .artefacts = artefacts,
              .statements = statements,
              .solutions = solutions,
              .test_plans = test_plans,
              .certifier = certifier} {}

  cms::generate::SqlCandidateSink candidates;
  cms::generate::SqlSpecificationSink specifications;
  cms::generate::SqlArtefactSink artefacts;
  cms::generate::SqlStatementSink statements;
  cms::generate::SqlSolutionSink solutions;
  cms::generate::SqlTestPlanSink test_plans;
  cms::generate::PipelinePorts ports;
};

/// @return The content tree that ships with the image: the vocabulary and the locale names both live there (ADR 0056).
auto facts() -> cms::generate::RunnerFacts {
  return cms::generate::RunnerFacts{.content_root = std::filesystem::path{CMS_CONTENT_DIR}};
}

/// @return A content root carrying one declared locale, so a task that ships another one has nowhere to read it from.
auto content_root_with_one_locale() -> std::filesystem::path {
  const auto root = std::filesystem::path{CMS_CONTENT_DIR};
  const auto limited = cms::testing::scratch_directory("cms-locales-limited");
  std::filesystem::create_directories(limited);
  std::filesystem::copy_file(root / "topics.json", limited / "topics.json",
                             std::filesystem::copy_options::overwrite_existing);
  auto file = std::ofstream{limited / "locales.json"};
  REQUIRE(file.good());
  // A synthetic language, so this fixture proves the refusal without naming a real one anywhere in the generator.
  file << R"({"version": 1, "locales": [{"tag": "zz", "name": "Zedish"}]})";
  return limited;
}

}  // namespace

TEST_CASE("a queued job runs every step and ends done on a verified certificate", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(RUN_TASK);
  auto store = cms::generate::test::MemoryStore{};
  // The real certifier: its three methods run here, including the half-limit margin that 0048 makes a certificate's
  // condition. It measures the reference's own peak, so the sanitizer-built test process cannot inflate the number.
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording();

  const auto report = cms::generate::run_pipeline(client, database, WORKER, wired.ports, facts());

  if (!report) {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(report.has_value());
  CHECK(report->job_id == job);
  CHECK(report->state == "done");
  CHECK(report->steps.size() == 7);
  // Four shards, the decision, the specification, the statement, the reference, two partials, three test programs.
  CHECK(client.prompts().size() == 13);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "done");
  CHECK(holder.empty());
  CHECK(attempts == 1);

  auto candidate_rows = std::int64_t{0};
  auto chosen = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationCandidates.chosen)
                                      .from(cms::db::generationCandidates)
                                      .where(cms::db::generationCandidates.jobId == job))) {
    ++candidate_rows;
    chosen += row.chosen ? 1 : 0;
  }
  CHECK(candidate_rows == 4);
  CHECK(chosen == 1);

  CHECK(refusals_of(database, job).empty());
}

TEST_CASE("a wrong reference ends the job failed at step 5", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(REFERENCE_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording(WRONG_REFERENCE);

  const auto report = cms::generate::run_pipeline(client, database, WORKER, wired.ports, facts());

  if (!report) {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 5") != std::string::npos);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  CHECK(attempts == 1);
  CHECK(verification_of(database, REFERENCE_TASK) == "pending");

  const auto refusals = refusals_of(database, job);
  REQUIRE(refusals.size() == 1);
  CHECK(refusals.front().first == 5);
}

TEST_CASE("a job with no step 3 artefact is refused by name", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(MISSING_TASK);
  const auto claimed = cms::generate::claim_job(database, WORKER);
  REQUIRE(claimed.has_value());
  REQUIRE(claimed->has_value());
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording();

  const auto report = cms::generate::run_second_half(client, database, WORKER, **claimed, wired.ports, facts());

  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 4") != std::string::npos);
  CHECK(report.error().message.find("step 3") != std::string::npos);
  CHECK(report.error().message.find("specification") != std::string::npos);
  // Nothing was recorded for step 4, and the job did not stay running.
  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  auto statement_rows = std::int64_t{0};
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.step, cms::db::generationArtefacts.kind)
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job))) {
    statement_rows += row.step == 4 && row.kind != "refusal" ? 1 : 0;
  }
  CHECK(statement_rows == 0);
  CHECK(client.prompts().empty());
}

TEST_CASE("a certificate that is not verified ends the job failed", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(CERTIFICATE_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = StubCertifier{CERTIFICATE_TASK, cms::generate::VerificationOutcome::failed,
                                 "the reference disagrees with the oracle at n = 100"};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording();

  const auto report = cms::generate::run_pipeline(client, database, WORKER, wired.ports, facts());

  if (!report) {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 7") != std::string::npos);
  CHECK(report.error().message.find("disagrees with the oracle") != std::string::npos);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  CHECK(verification_of(database, CERTIFICATE_TASK) == "pending");
  const auto refusals = refusals_of(database, job);
  REQUIRE(refusals.size() == 1);
  CHECK(refusals.front().first == 7);
  CHECK(refusals.front().second.find("disagrees with the oracle") != std::string::npos);
}

TEST_CASE("a step's refusal ends the job failed and is recorded on the job", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(REFUSAL_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  // The first shard answers with a plan that names no approach, which step 1 refuses by name.
  auto client = cms::generate::RecordedClient{
      "deployer-model", "prompt-v1", 7,
      {"{\"approach\": \"\", \"intended_complexity\": \"O(n log n)\", \"fits_bounds\": true, "
       "\"trap\": \"a scan is fast enough\"}"}};

  const auto report = cms::generate::run_pipeline(client, database, WORKER, wired.ports, facts());

  if (report) {
    UNSCOPED_INFO("the run returned job " + std::to_string(report->job_id) + " state " + report->state);
  }
  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 1") != std::string::npos);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  const auto refusals = refusals_of(database, job);
  REQUIRE(refusals.size() == 1);
  CHECK(refusals.front().first == 1);
  CHECK(refusals.front().second.find("missing or has wrong: approach") != std::string::npos);
}

TEST_CASE("a task statement in a locale the content does not declare is refused by name", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(LOCALE_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = StubCertifier{LOCALE_TASK, cms::generate::VerificationOutcome::verified};
  auto wired = Wired{certifier, store};
  auto client = whole_job_recording();
  // Steps 1 to 3 record their evidence, then step 4 finds its language is not declared beside topics.json: the task
  // ships "en" and this root declares only "th".
  const auto content_root = content_root_with_one_locale();

  const auto report = cms::generate::run_pipeline(
      client, database, WORKER, wired.ports,
      cms::generate::RunnerFacts{.content_root = content_root});

  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("step 4") != std::string::npos);
  CHECK(report.error().message.find("locales.json does not declare") != std::string::npos);
  CHECK(report.error().message.find("\"en\"") != std::string::npos);
  // No prompt was built for the statement: the four plan calls and the two before it, and nothing after.
  CHECK(client.prompts().size() == 6);
  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
}

TEST_CASE("the brief names the task's own difficulty, topics and technique", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = seed_job(BRIEF_TASK);
  auto claim = cms::generate::JobClaim{
      .id = job, .task_id = BRIEF_TASK, .task_version = 1, .attempts = 1, .worker = WORKER};

  const auto brief = cms::generate::load_brief(database, claim);

  REQUIRE(brief.has_value());
  CHECK(brief->technique == "prefix-sums");
  CHECK(brief->difficulty == "900");
  CHECK(brief->topics == std::vector<std::string>{"prefix-sums"});
  CHECK(brief->locales == std::vector<std::string>{"en"});
}

TEST_CASE("the binary's path drives a queued job to done through a real certificate", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  REQUIRE(cms::seal::configure(TEST_SEAL_KEY).has_value());
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(BINARY_PATH_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  auto server = LocalModelServer{whole_job_answers()};

  // The record an operator adds: sealed key, the protocol the adapter speaks, the endpoint the server answers on.
  write_model_record(database, "probe-model", server.base_url(), "probe-model-1", sealed("probe-key"));

  // The client the binary builds: it reads the record on its first call, opens the key with the deployment's sealer and
  // talks to the endpoint over a socket. No RecordedClient, no stub, and the certificate below is written by the real
  // certifier because Wired carries SqlCertifier.
  auto model = cms::generate::RecordBackedClient{
      std::make_shared<const cms::db::connection_config>(
          cms::db::connection_config{.client = cms::generate::test::live_client()}),
      "prompt-v1", 7};

  const auto report = cms::generate::run_pipeline(model, database, WORKER, wired.ports, facts());
  if (!report) {
    UNSCOPED_INFO(report.error().message);
  }
  REQUIRE(report.has_value());
  if (report->state != "done") {
    // A failure here is a certificate that refused, and its checks say which margin it broke: print them, because a bare
    // "not done" under a loaded machine says nothing about whether the product or the harness was tight.
    for (const auto& check : checks_of(database, job)) {
      UNSCOPED_INFO("the certificate said: " + check);
    }
    UNSCOPED_INFO("refusals: " + std::to_string(refusals_of(database, job).size()));
  }
  CHECK(report->state == "done");
  CHECK(report->steps.size() == 7);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "done");
  CHECK(holder.empty());

  const auto certificates = certificates_of(database, job);
  REQUIRE(certificates == 1);
  const auto outcome = verification_of(database, BINARY_PATH_TASK);
  CHECK(outcome == "verified");
  CHECK(server.requests() >= 12);

  // The record names the model the artefacts were produced by, without a new column.
  CHECK(last_artefact_model_id(database, job) == "probe-model-1");
  write_model_record_clear(database);
}

TEST_CASE("a disabled model record ends the job failed, naming what the operator has to fix", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  REQUIRE(cms::seal::configure(TEST_SEAL_KEY).has_value());
  auto database = cms::generate::test::live_database();
  drain_queue(database);
  const auto job = seed_job(DISABLED_TASK);
  auto store = cms::generate::test::MemoryStore{};
  auto certifier = cms::generate::SqlCertifier{*cms::generate::test::live_client(), store, COMMIT};
  auto wired = Wired{certifier, store};
  // The record exists and is the default, and an operator has turned it off: the worker must refuse by name rather than
  // call it, and the job must end failed with that refusal instead of sitting queued or running.
  write_model_record(database, "probe-model", "http://127.0.0.1:9/v1", "probe-model-1", sealed("probe-key"), false);

  auto model = cms::generate::RecordBackedClient{
      std::make_shared<const cms::db::connection_config>(
          cms::db::connection_config{.client = cms::generate::test::live_client()}),
      "prompt-v1", 7};

  const auto report = cms::generate::run_pipeline(model, database, WORKER, wired.ports, facts());
  REQUIRE(!report.has_value());
  CHECK(report.error().message.find("no enabled model record") != std::string::npos);

  const auto [state, holder, attempts] = job_row(database, job);
  CHECK(state == "failed");
  CHECK(holder.empty());
  const auto refusals = refusals_of(database, job);
  REQUIRE(!refusals.empty());
  CHECK(refusals.front().second.find("no enabled model record") != std::string::npos);
  CHECK(certificates_of(database, job) == 0);
  write_model_record_clear(database);
}
