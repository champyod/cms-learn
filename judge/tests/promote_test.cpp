// The publisher's one source is the certificate row the certifier wrote: the task file's own claim is named in a
// refusal and never believed. The reference check still runs afterwards, so a certificate cannot bless a case whose
// expected output its own reference does not reproduce.

#include <cms/judge/promote.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cms/testing/scratch_directory.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view COPYING_PROGRAM = "/bin/cat";
constexpr std::string_view CASE_TEXT = "43\n";
constexpr std::string_view TASK_ID = "probe-task";

auto write_text(const std::filesystem::path& path, std::string_view text) -> void {
  std::filesystem::create_directories(path.parent_path());
  auto stream = std::ofstream{path, std::ios::trunc};
  stream << text;
  REQUIRE(stream.good());
}

auto read_text(const std::filesystem::path& path) -> std::string {
  auto stream = std::ifstream{path, std::ios::binary};
  return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

/// The manifest the generator writes, with the two fields this case varies: the task's own status, and the claim
/// its file carries about verification.
auto manifest_text(std::string_view status, std::string_view verification) -> std::string {
  return std::string{"{\n  \"id\": \""} + std::string{TASK_ID} + "\",\n  \"version\": 1,\n  \"status\": \"" +
         std::string{status} + "\",\n  \"title\": { \"en\": \"Probe\", \"th\": \"ทดสอบ\" },\n" +
         "  \"verification\": { \"status\": \"" + std::string{verification} +
         "\", \"reference_solution\": \"solutions/main.cpp\", \"methods\": [] }\n}\n";
}

auto make_task(const std::filesystem::path& task_dir, std::string_view status,
               std::string_view verification) -> void {
  write_text(task_dir / "task.json", manifest_text(status, verification));
  write_text(task_dir / "examples" / "1.in", CASE_TEXT);
  write_text(task_dir / "examples" / "1.out", CASE_TEXT);
}

auto copy_program(const std::filesystem::path& from, const std::filesystem::path& to) -> void {
  std::filesystem::create_directories(to.parent_path());
  std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
  std::filesystem::permissions(to,
                               std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::add);
}

auto remove_tree(const std::filesystem::path& root) -> void {
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

/// What the publisher asked the source for, and what the certifier left behind: one row, or none.
class MemoryCertificates final : public cms::judge::CertificateSource {
 public:
  struct Asked {
    std::string task_id;
    std::int64_t task_version{0};
  };

  explicit MemoryCertificates(std::optional<cms::judge::Certificate> row) : row_(std::move(row)) {}

  [[nodiscard]] auto latest(std::string_view task_id, std::int64_t task_version) const
      -> cms::Result<std::optional<cms::judge::Certificate>> override {
    asked.push_back(Asked{.task_id = std::string{task_id}, .task_version = task_version});
    return row_;
  }

  [[nodiscard]] auto questions() const -> const std::vector<Asked>& { return asked; }

 private:
  mutable std::vector<Asked> asked;
  std::optional<cms::judge::Certificate> row_;
};

}  // namespace

TEST_CASE("a certificate that says verified publishes the draft", "[promote]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-promote-verified");
  const auto task = root / "tasks" / std::string{TASK_ID};
  make_task(task, "draft", "pending");
  const auto artifacts = root / "artifacts";
  const auto reference = root / "bin" / "probe";
  copy_program(COPYING_PROGRAM, reference);
  const auto certificates =
      MemoryCertificates{cms::judge::Certificate{.outcome = "verified", .commit_id = "a1b2c3d4"}};

  const auto promoted = cms::judge::promote_task(task, artifacts, reference, certificates);

  REQUIRE(promoted.has_value());
  const auto published = read_text(task / "task.json");
  CHECK(published.find("\"status\": \"published\"") != std::string::npos);
  CHECK(promoted->artifact.filename().string() == promoted->digest + ".json");
  CHECK(read_text(promoted->artifact) == published);
  REQUIRE(certificates.questions().size() == 1);
  CHECK(certificates.questions().front().task_id == std::string{TASK_ID});
  CHECK(certificates.questions().front().task_version == 1);
  remove_tree(root);
}

TEST_CASE("a file that says verified and a certificate that says failed is refused, naming both", "[promote]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-promote-disagree");
  const auto task = root / "tasks" / std::string{TASK_ID};
  make_task(task, "draft", "verified");
  const auto artifacts = root / "artifacts";
  const auto reference = root / "bin" / "probe";
  copy_program(COPYING_PROGRAM, reference);
  const auto certificates =
      MemoryCertificates{cms::judge::Certificate{.outcome = "failed", .commit_id = "deadbeef"}};

  const auto promoted = cms::judge::promote_task(task, artifacts, reference, certificates);

  REQUIRE(!promoted.has_value());
  const auto& message = promoted.error().message;
  CHECK(message.find("the task file says verification \"verified\"") != std::string::npos);
  CHECK(message.find("the certificate row records \"failed\"") != std::string::npos);
  CHECK(message.find("deadbeef") != std::string::npos);
  CHECK(!std::filesystem::exists(artifacts));
  CHECK(read_text(task / "task.json").find("\"status\": \"draft\"") != std::string::npos);
  remove_tree(root);
}

TEST_CASE("the certificate row decides, so a file that still says pending publishes", "[promote]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-promote-row-wins");
  const auto task = root / "tasks" / std::string{TASK_ID};
  make_task(task, "draft", "pending");
  const auto artifacts = root / "artifacts";
  const auto reference = root / "bin" / "probe";
  copy_program(COPYING_PROGRAM, reference);
  const auto certificates =
      MemoryCertificates{cms::judge::Certificate{.outcome = "verified", .commit_id = "a1b2c3d4"}};

  const auto promoted = cms::judge::promote_task(task, artifacts, reference, certificates);

  REQUIRE(promoted.has_value());
  CHECK(read_text(task / "task.json").find("\"status\": \"published\"") != std::string::npos);
  remove_tree(root);
}

TEST_CASE("a task whose certificate row is missing is refused, naming the version it read for", "[promote]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-promote-missing-row");
  const auto task = root / "tasks" / std::string{TASK_ID};
  make_task(task, "draft", "verified");
  const auto artifacts = root / "artifacts";
  const auto reference = root / "bin" / "probe";
  copy_program(COPYING_PROGRAM, reference);
  const auto certificates = MemoryCertificates{std::nullopt};

  const auto promoted = cms::judge::promote_task(task, artifacts, reference, certificates);

  REQUIRE(!promoted.has_value());
  CHECK(promoted.error().message.find("no certificate row exists for probe-task version 1") != std::string::npos);
  CHECK(!std::filesystem::exists(artifacts));
  remove_tree(root);
}

TEST_CASE("an already published task is refused before a certificate is read", "[promote]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-promote-published");
  const auto task = root / "tasks" / std::string{TASK_ID};
  make_task(task, "published", "verified");
  const auto artifacts = root / "artifacts";
  const auto reference = root / "bin" / "probe";
  copy_program(COPYING_PROGRAM, reference);
  const auto certificates =
      MemoryCertificates{cms::judge::Certificate{.outcome = "verified", .commit_id = "a1b2c3d4"}};

  const auto promoted = cms::judge::promote_task(task, artifacts, reference, certificates);

  REQUIRE(!promoted.has_value());
  CHECK(promoted.error().message.find("is already published") != std::string::npos);
  CHECK(certificates.questions().empty());
  remove_tree(root);
}

TEST_CASE("a certified task whose reference does not reproduce its case is still refused", "[promote]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-promote-reference");
  const auto task = root / "tasks" / std::string{TASK_ID};
  make_task(task, "draft", "verified");
  write_text(task / "examples" / "1.out", "not what the reference prints\n");
  const auto artifacts = root / "artifacts";
  const auto reference = root / "bin" / "probe";
  copy_program(COPYING_PROGRAM, reference);
  const auto certificates =
      MemoryCertificates{cms::judge::Certificate{.outcome = "verified", .commit_id = "a1b2c3d4"}};

  const auto promoted = cms::judge::promote_task(task, artifacts, reference, certificates);

  REQUIRE(!promoted.has_value());
  CHECK(!std::filesystem::exists(artifacts));
  remove_tree(root);
}
