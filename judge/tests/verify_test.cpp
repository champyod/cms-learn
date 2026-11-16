// A task's identity is its reference's bytes, not the path those bytes sit at: two checkouts of the same tree must
// describe the same task, and a changed reference must change the digest. The fixture uses a real executable at two
// paths, because a copied file with the same bytes is what a second checkout actually holds.

#include <cms/judge/verify.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cms/testing/scratch_directory.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

/// A program that copies its input to its output satisfies this case, which keeps the fixture about paths and
/// bytes rather than about behaviour.
constexpr auto COPYING_PROGRAM = "/bin/cat";

constexpr std::string_view CASE_TEXT = "42\n";

auto write_text(const std::filesystem::path& path, std::string_view text) -> void {
  std::filesystem::create_directories(path.parent_path());
  auto stream = std::ofstream{path, std::ios::trunc};
  stream << text;
  REQUIRE(stream.good());
}

auto make_task(const std::filesystem::path& task_dir) -> void {
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

}  // namespace

TEST_CASE("a reference's digest is its bytes, not the path it sits at", "[verify]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-verify-digest");
  const auto first_task = root / "checkout-one" / "content" / "tasks" / "probe-task";
  const auto second_task = root / "checkout-two" / "content" / "tasks" / "probe-task";
  make_task(first_task);
  make_task(second_task);
  const auto first_reference = root / "checkout-one" / "bin" / "probe";
  const auto second_reference = root / "checkout-two" / "somewhere" / "else" / "probe";
  copy_program(COPYING_PROGRAM, first_reference);
  copy_program(COPYING_PROGRAM, second_reference);

  const auto one = cms::judge::verify_task(first_task, first_reference);
  const auto two = cms::judge::verify_task(second_task, second_reference);

  REQUIRE(one.has_value());
  REQUIRE(two.has_value());
  CHECK(one->task == "probe-task");
  CHECK(two->task == "probe-task");
  CHECK(one->reference_digest == two->reference_digest);
  REQUIRE(one->cases.size() == 1);
  REQUIRE(two->cases.size() == 1);
  CHECK(one->cases.front().input_digest == two->cases.front().input_digest);
  CHECK(one->cases.front().output_digest == two->cases.front().output_digest);
  remove_tree(root);
}

TEST_CASE("a changed reference changes the digest", "[verify]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-verify-changed");
  const auto task = root / "content" / "tasks" / "probe-task";
  make_task(task);
  const auto reference = root / "bin" / "probe";
  copy_program(COPYING_PROGRAM, reference);
  const auto before = cms::judge::verify_task(task, reference);
  REQUIRE(before.has_value());

  {  // A byte appended after the program's code changes the file without changing what the program prints.
    auto stream = std::ofstream{reference, std::ios::binary | std::ios::app};
    stream << "a changed byte";
    REQUIRE(stream.good());
  }
  const auto after = cms::judge::verify_task(task, reference);

  REQUIRE(after.has_value());
  CHECK(after->reference_digest != before->reference_digest);
  remove_tree(root);
}

TEST_CASE("a task directory named with a trailing separator still names its task", "[verify]") {
  REQUIRE(std::filesystem::exists(COPYING_PROGRAM));
  const auto root = cms::testing::scratch_directory("cms-verify-separator");
  const auto task = root / "content" / "tasks" / "probe-task";
  make_task(task);
  const auto reference = root / "bin" / "probe";
  copy_program(COPYING_PROGRAM, reference);

  const auto named_with_separator = std::filesystem::path{task.string() + "/"};
  const auto manifest = cms::judge::verify_task(named_with_separator, reference);

  REQUIRE(manifest.has_value());
  CHECK(manifest->task == "probe-task");
  remove_tree(root);
}
