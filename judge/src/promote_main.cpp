// Publishes one draft task, after the reference has been shown to satisfy every case.
//
// The promotion is the only path from draft to published, so it carries the checks: pending verification is
// refused before anything is read, and a reference that does not reproduce its expected output stops it.

#include <cstdio>
#include <filesystem>
#include <print>
#include <string_view>

#include <cms/judge/promote.hpp>

namespace {

constexpr std::string_view USAGE =
    "usage: cms_promote_task <task-dir> <artifact-dir> <reference-exe>";

auto fail(std::string_view message) -> int {
  std::fprintf(stderr, "cms-promote-task: %.*s\n", static_cast<int>(message.size()), message.data());
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    return fail(USAGE);
  }
  const auto promoted = cms::judge::promote_task(std::filesystem::path{argv[1]},
                                                 std::filesystem::path{argv[2]},
                                                 std::filesystem::path{argv[3]});
  if (!promoted) {
    return fail(promoted.error().message);
  }
  std::println("published {} -> {}", promoted->digest, promoted->artifact.string());
  return 0;
}
