// Writes one draft task directory from an explicit specification.
//
// Every argument is a fact about the task, so the output is a pure function of the command line: the same
// invocation twice produces byte-identical files, which is what makes a draft reviewable as a diff.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <cms/judge/generate.hpp>

namespace {

constexpr std::string_view USAGE =
    "usage: cms_generate_task --out <dir> --id <id> --title-en <t> --title-th <t> --topics <a,b>\\n"
    "         --difficulty <n> --time-ms <n> --memory-mb <n> --checker <kind>\\n"
    "         --statement-en <file> --statement-th <file> --example-in <file>\\n"
    "         --example-out <file> --solution <file>";

auto fail(std::string_view message) -> int {
  std::fprintf(stderr, "cms-generate-task: %.*s\n", static_cast<int>(message.size()), message.data());
  return 1;
}

auto read_file(const std::filesystem::path& path) -> std::string {
  auto stream = std::ifstream{path};
  if (!stream) {
    return {};
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  auto text = buffer.str();
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

auto split_topics(const std::string& text) -> std::vector<std::string> {
  auto topics = std::vector<std::string>{};
  auto start = std::size_t{0};
  while (start <= text.size()) {
    const auto comma = text.find(',', start);
    const auto piece = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!piece.empty()) {
      topics.push_back(piece);
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return topics;
}

}  // namespace

int main(int argc, char** argv) {
  auto spec = cms::judge::TaskSpec{};
  auto output = std::filesystem::path{};

  for (auto index = 1; index + 1 < argc; index += 2) {
    const auto flag = std::string_view{argv[index]};
    const auto value = std::string{argv[index + 1]};
    if (flag == "--out") output = value;
    else if (flag == "--id") spec.id = value;
    else if (flag == "--title-en") spec.title_en = value;
    else if (flag == "--title-th") spec.title_th = value;
    else if (flag == "--topics") spec.topics = split_topics(value);
    else if (flag == "--difficulty") spec.difficulty = std::stoi(value);
    else if (flag == "--time-ms") spec.time_ms = std::stoi(value);
    else if (flag == "--memory-mb") spec.memory_mb = std::stoi(value);
    else if (flag == "--checker") spec.checker = value;
    else if (flag == "--statement-en") spec.statement_en = read_file(value);
    else if (flag == "--statement-th") spec.statement_th = read_file(value);
    else if (flag == "--example-in") spec.example_input = read_file(value);
    else if (flag == "--example-out") spec.example_output = read_file(value);
    else if (flag == "--solution") spec.solution = read_file(value);
    else return fail(USAGE);
  }
  if (output.empty()) {
    return fail(USAGE);
  }

  const auto written = cms::judge::generate_task(spec, output);
  if (!written) {
    return fail(written.error().message);
  }
  std::println("generated {}", output.string());
  return 0;
}
