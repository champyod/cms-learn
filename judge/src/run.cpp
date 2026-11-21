#include <cms/judge/run.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace cms::judge {
namespace {

constexpr std::string_view COMPILE_INPUT = "compile.in";
constexpr std::string_view COMPILE_OUTPUT = "compile.out";
constexpr std::string_view COMPILE_ERROR = "compile.err";
constexpr std::string_view COMPILE_META = "compile.meta";
constexpr std::string_view PROGRAM = "./solution";
constexpr std::string_view SOURCE = "main.cpp";

auto read_text(const std::filesystem::path& path) -> std::string {
  auto stream = std::ifstream{path, std::ios::binary};
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

auto write_text(const std::filesystem::path& path, std::string_view text) -> bool {
  auto stream = std::ofstream{path, std::ios::trunc | std::ios::binary};
  stream << text;
  return static_cast<bool>(stream);
}

constexpr std::int64_t KILOBYTES_PER_MEGABYTE = 1024;

/// Kilobytes rounded up to whole megabytes, so a message never claims less than was measured.
auto megabytes(std::int64_t kilobytes) -> std::int64_t {
  return (kilobytes + KILOBYTES_PER_MEGABYTE - 1) / KILOBYTES_PER_MEGABYTE;
}

auto trimmed(std::string_view text) -> std::string_view {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

/// The first non-blank line, for a detail sentence that names the compiler's reason without a whole log.
auto first_line(std::string_view text) -> std::string {
  auto cursor = std::size_t{0};
  while (cursor < text.size()) {
    const auto end = text.find('\n', cursor);
    const auto line = text.substr(cursor, end == std::string_view::npos ? text.size() - cursor
                                                                       : end - cursor);
    const auto first = line.find_first_not_of(" \t\r");
    if (first != std::string_view::npos) {
      return std::string{line.substr(first, line.find_last_not_of(" \t\r") - first + 1)};
    }
    if (end == std::string_view::npos) {
      break;
    }
    cursor = end + 1;
  }
  return {};
}

auto compile_run(const RunSettings& settings, const TaskRun& task,
                 const std::filesystem::path& box) -> RunSpec {
  auto spec = RunSpec{};
  spec.box_id = settings.box_id;
  spec.box_dir = box;
  spec.program = settings.compiler;
  spec.arguments = {"-std=" + task.cpp_standard, "-O2", "-pipe", "-o", std::string{PROGRAM},
                    std::string{SOURCE}};
  spec.time_limit_ms = settings.compile_time_limit_ms;
  spec.wall_limit_ms = settings.compile_time_limit_ms + 10'000;
  spec.memory_reserved_limit_kb = settings.compile_memory_kb;
  spec.process_limit = 64;
  spec.input = std::string{COMPILE_INPUT};
  spec.output = std::string{COMPILE_OUTPUT};
  spec.error = std::string{COMPILE_ERROR};
  spec.meta = std::string{COMPILE_META};
  return spec;
}

auto case_run(const RunSettings& settings, const TaskRun& task, const std::filesystem::path& box)
    -> RunSpec {
  auto spec = RunSpec{};
  spec.box_id = settings.box_id;
  spec.box_dir = box;
  spec.program = std::string{PROGRAM};
  spec.time_limit_ms = task.time_limit_ms;
  spec.wall_limit_ms = task.time_limit_ms * 2 + 5'000;
  spec.memory_reserved_limit_kb = task.memory_reserved_limit_kb;
  return spec;
}

}  // namespace

auto database_verdict(SubmissionVerdict verdict) -> std::string_view {
  switch (verdict) {
    case SubmissionVerdict::accepted:
      return "ok";
    case SubmissionVerdict::wrong_answer:
      return "wrong_answer";
    case SubmissionVerdict::wrong_format:
      return "wrong_format";
    case SubmissionVerdict::partially_correct:
      return "partially_correct";
    case SubmissionVerdict::compile_error:
    case SubmissionVerdict::runtime_error:
    case SubmissionVerdict::time_limit_exceeded:
    case SubmissionVerdict::memory_limit_exceeded:
    case SubmissionVerdict::failed:
      return "failed";
    case SubmissionVerdict::checker_failed:
      return "check_failed";
  }
  return "failed";
}

auto submission_verdict_name(SubmissionVerdict verdict) -> std::string_view {
  switch (verdict) {
    case SubmissionVerdict::accepted:
      return "accepted";
    case SubmissionVerdict::wrong_answer:
      return "wrong_answer";
    case SubmissionVerdict::wrong_format:
      return "wrong_format";
    case SubmissionVerdict::partially_correct:
      return "partially_correct";
    case SubmissionVerdict::compile_error:
      return "compile_error";
    case SubmissionVerdict::runtime_error:
      return "runtime_error";
    case SubmissionVerdict::time_limit_exceeded:
      return "time_limit_exceeded";
    case SubmissionVerdict::memory_limit_exceeded:
      return "memory_limit_exceeded";
    case SubmissionVerdict::checker_failed:
      return "checker_failed";
    case SubmissionVerdict::failed:
      return "failed";
  }
  return "failed";
}

auto sandbox_verdict(const RunOutcome& outcome) -> Result<std::optional<SubmissionVerdict>> {
  if (outcome.status == "OK") {
    return std::optional<SubmissionVerdict>{};
  }
  if (outcome.status == "TO") {
    return SubmissionVerdict::time_limit_exceeded;
  }
  if (outcome.status == "ML") {
    return SubmissionVerdict::memory_limit_exceeded;
  }
  if (outcome.status == "SG" || outcome.status == "RE") {
    return SubmissionVerdict::runtime_error;
  }
  if (outcome.status == "XX") {
    return std::unexpected(Error::make(
        ErrorCode::unavailable,
        "the sandbox reported an internal error; the submission stays claimable"));
  }
  if (outcome.is_killed) {
    return SubmissionVerdict::time_limit_exceeded;
  }
  return std::unexpected(Error::make(
      ErrorCode::unavailable, "the sandbox reported no status; the run did not happen"));
}

/// The verdict a run's two memory limits earn, and the sentence naming the one it exceeded.
auto memory_verdict(const RunOutcome& outcome, std::int64_t used_limit_kb, std::int64_t reserved_limit_kb)
    -> Result<std::optional<Judgement>> {
  if (outcome.status == "ML") {
    return Judgement{
        .verdict = SubmissionVerdict::memory_limit_exceeded,
        .detail = "reserved memory: the program mapped more than the " +
                  std::to_string(megabytes(reserved_limit_kb)) + " MB reserved limit"};
  }
  if (outcome.status != "OK" || outcome.is_killed) {
    return std::nullopt;
  }
  const auto entry = outcome.meta.find("max-rss");
  if (entry == outcome.meta.end()) {
    return std::unexpected(Error::make(
        ErrorCode::unavailable,
        "the sandbox reported no peak resident memory, so the used limit cannot be enforced; the submission stays "
        "claimable"));
  }
  const auto text = trimmed(entry->second);
  auto kilobytes = std::int64_t{0};
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), kilobytes);
  if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return std::unexpected(Error::make(
        ErrorCode::unavailable,
        "the sandbox reported a peak resident memory this judge cannot read (\"" + std::string{text} +
            "\"); the submission stays claimable"));
  }
  if (kilobytes > used_limit_kb) {
    return Judgement{.verdict = SubmissionVerdict::memory_limit_exceeded,
                     .detail = "used memory: the program touched " + std::to_string(megabytes(kilobytes)) +
                               " MB, above the " + std::to_string(megabytes(used_limit_kb)) + " MB used limit"};
  }
  return std::nullopt;
}

auto compile_verdict(const RunOutcome& outcome) -> std::optional<SubmissionVerdict> {
  if (outcome.status == "OK" && !outcome.is_killed) {
    return std::nullopt;
  }
  return SubmissionVerdict::compile_error;
}

auto checker_verdict(Verdict verdict) -> SubmissionVerdict {
  switch (verdict) {
    case Verdict::ok:
      return SubmissionVerdict::accepted;
    case Verdict::wrong_answer:
      return SubmissionVerdict::wrong_answer;
    case Verdict::wrong_format:
      return SubmissionVerdict::wrong_format;
    case Verdict::partially_correct:
      return SubmissionVerdict::partially_correct;
    case Verdict::failed:
      return SubmissionVerdict::failed;
    case Verdict::check_failed:
      return SubmissionVerdict::checker_failed;
  }
  return SubmissionVerdict::checker_failed;
}

auto judge_in_box(const RunSettings& settings, const TaskRun& task, std::string_view source,
                  const std::filesystem::path& box) -> Result<Judgement> {
  if (!write_text(box / std::string{SOURCE}, source) ||
      !write_text(box / std::string{COMPILE_INPUT}, "")) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "cannot stage the submission in the box"));
  }

  const auto compiled = run_sandboxed(compile_run(settings, task, box), settings.isolate);
  if (!compiled) {
    return std::unexpected(compiled.error());
  }
  if (const auto verdict = compile_verdict(*compiled)) {
    const auto reason = first_line(read_text(box / std::string{COMPILE_ERROR}));
    auto detail = std::string{submission_verdict_name(*verdict)};
    if (!reason.empty()) {
      detail += ": " + reason;
    }
    return Judgement{.verdict = *verdict, .detail = std::move(detail)};
  }
  if (!std::filesystem::exists(box / std::string{PROGRAM})) {
    return Judgement{.verdict = SubmissionVerdict::compile_error,
                     .detail = "the compiler reported success but wrote no solution"};
  }

  const auto input_path = box / "input.txt";
  const auto answer_path = box / "answer.txt";
  std::error_code discarded;
  for (const auto& one : task.cases) {
    if (!write_text(input_path, one.input) || !write_text(answer_path, one.expected_output)) {
      return std::unexpected(
          Error::make(ErrorCode::unavailable, "cannot stage a case in the box"));
    }
    std::filesystem::remove(box / "output.txt", discarded);

    const auto ran = run_sandboxed(case_run(settings, task, box), settings.isolate);
    if (!ran) {
      return std::unexpected(ran.error());
    }
    const auto sandbox = sandbox_verdict(*ran);
    if (!sandbox) {
      return std::unexpected(sandbox.error());
    }
    const auto memory =
        memory_verdict(*ran, task.memory_used_limit_kb, task.memory_reserved_limit_kb);
    if (!memory) {
      return std::unexpected(memory.error());
    }
    if (*memory) {
      return **memory;
    }
    if (*sandbox) {
      return Judgement{.verdict = **sandbox,
                       .detail = std::string{submission_verdict_name(**sandbox)}};
    }

    const auto checked = run_checker(task.checker, input_path.string(),
                                     (box / "output.txt").string(), answer_path.string());
    if (!checked) {
      return Judgement{.verdict = SubmissionVerdict::checker_failed,
                       .detail = "the checker produced no verdict: " + checked.error().message};
    }
    if (*checked != Verdict::ok) {
      return Judgement{.verdict = checker_verdict(*checked),
                       .detail = std::string{verdict_name(*checked)}};
    }
  }
  return Judgement{.verdict = SubmissionVerdict::accepted,
                   .detail = "every case matched the answer"};
}

auto judge_submission(const RunSettings& settings, const TaskRun& task, std::string_view source)
    -> Result<Judgement> {
  if (task.cases.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "the task has no cases; nothing would be proven"));
  }
  const auto box = prepare_box(settings.box_id, settings.isolate);
  if (!box) {
    return std::unexpected(box.error());
  }
  const auto judgement = judge_in_box(settings, task, source, *box);
  const auto cleaned = discard_box(settings.box_id, settings.isolate);
  if (!judgement) {
    return std::unexpected(judgement.error());
  }
  // A box that could not be cleaned up holds a UID range until someone notices; report it, never swallow it.
  if (!cleaned) {
    return std::unexpected(cleaned.error());
  }
  return judgement;
}

}  // namespace cms::judge
