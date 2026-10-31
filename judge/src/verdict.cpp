#include <cms/judge/verdict.hpp>

#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

namespace cms::judge {
namespace {

/// testlib 0.9.41's own words, matched as a prefix because its message continues after the token
/// ("wrong answer 1st words differ"). The list is copied from the header that writes it — `_pe` really
/// does say "wrong output format", and `_fail` really does say "FAIL".
struct Token {
  std::string_view text;
  Verdict verdict;
};

constexpr std::array<Token, 5> TOKENS{{
    {"wrong answer", Verdict::wrong_answer},
    {"wrong output format", Verdict::wrong_format},
    {"points", Verdict::partially_correct},
    {"fail", Verdict::failed},
    {"ok", Verdict::ok},
}};

auto lowered(std::string_view text) -> std::string {
  auto result = std::string{};
  result.reserve(text.size());
  for (const auto character : text) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

auto trimmed(std::string_view text) -> std::string_view {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

/// Forks and execs the checker with its standard error on @p write_fd. Never returns in the child.
auto spawn_checker(const std::string& checker, const std::string& input, const std::string& output,
                   const std::string& answer, int write_fd) -> pid_t {
  const auto child = ::fork();
  if (child != 0) {
    return child;
  }
  ::dup2(write_fd, STDERR_FILENO);
  ::execl(checker.c_str(), checker.c_str(), input.c_str(), output.c_str(), answer.c_str(), nullptr);
  ::_exit(127);
}

auto drain(int read_fd, std::string& collected) -> void {
  auto buffer = std::array<char, 4096>{};
  while (true) {
    const auto count = ::read(read_fd, buffer.data(), buffer.size());
    if (count <= 0) {
      return;
    }
    collected.append(buffer.data(), static_cast<std::size_t>(count));
  }
}

/// Runs the checker with the three files testlib expects and collects its standard error.
/// Returns false when the checker did not exit zero, which the caller reads as a failed check.
auto capture_checker_error(const std::string& checker, const std::string& input,
                           const std::string& output, const std::string& answer,
                           std::string& collected) -> bool {
  auto ends = std::array<int, 2>{-1, -1};
  if (::pipe(ends.data()) != 0) {
    return false;
  }
  const auto child =
      spawn_checker(checker, input, output, answer, ends[1]);
  if (child < 0) {
    ::close(ends[0]);
    ::close(ends[1]);
    return false;
  }
  ::close(ends[1]);
  drain(ends[0], collected);
  ::close(ends[0]);

  auto status = 0;
  if (::waitpid(child, &status, 0) != child) {
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

auto parse_checker_output(std::string_view last_line, int exit_code) -> Verdict {
  if (exit_code != 0) {
    return Verdict::check_failed;
  }
  const auto line = lowered(trimmed(last_line));
  if (line.empty()) {
    return Verdict::check_failed;
  }
  for (const auto& token : TOKENS) {
    if (line.starts_with(token.text)) {
      return token.verdict;
    }
  }
  return Verdict::check_failed;
}

auto run_checker(const std::string& checker, const std::string& input, const std::string& output,
                 const std::string& answer) -> Result<Verdict> {
  auto collected = std::string{};
  const auto is_clean = capture_checker_error(checker, input, output, answer, collected);

  const auto trimmed_output = trimmed(collected);
  const auto last_break = trimmed_output.rfind('\n');
  const auto last_line = last_break == std::string_view::npos
                             ? trimmed_output
                             : trimmed(trimmed_output.substr(last_break + 1));

  if (collected.empty() && !is_clean) {
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       "the checker " + checker + " produced no verdict"));
  }
  return parse_checker_output(last_line, is_clean ? 0 : 1);
}

auto verdict_name(Verdict verdict) -> std::string_view {
  switch (verdict) {
    case Verdict::ok:
      return "ok";
    case Verdict::wrong_answer:
      return "wrong_answer";
    case Verdict::wrong_format:
      return "wrong_format";
    case Verdict::partially_correct:
      return "partially_correct";
    case Verdict::failed:
      return "failed";
    case Verdict::check_failed:
      return "check_failed";
  }
  return "check_failed";
}

}  // namespace cms::judge
