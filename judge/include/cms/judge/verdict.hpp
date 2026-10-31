#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <cms/error.hpp>

namespace cms::judge {

/// @brief What a checker decided about one submission.
///
/// The vocabulary is testlib 0.9.41's own, read from the vendored header rather than assumed: `_ok` writes
/// `ok `, `_wa` writes `wrong answer `, `_pe` writes `wrong output format `, `_fail` writes `FAIL `, and a
/// points verdict writes `points `. The last enumerator is ours: a checker that exited non-zero, said
/// nothing, or said something outside that set is a failed check, and it must never be read as an
/// acceptance — which is why no enumerator here is a default-constructible "ok".
enum class Verdict : std::uint8_t {
  ok,
  wrong_answer,
  wrong_format,
  partially_correct,
  failed,
  /// The checker exited non-zero, said nothing, or said something this judge does not recognise.
  check_failed,
};

/// @brief The verdict a checker's last line and exit status amount to.
///
/// @param last_line The last non-empty line the checker wrote to standard error.
/// @param exit_code The checker's exit status. Anything but zero is a failed check.
/// @return A verdict, never inferred from silence: an unrecognised line is check_failed.
[[nodiscard]] Verdict parse_checker_output(std::string_view last_line, int exit_code);

/// @brief Runs a checker over one case and reads its verdict.
///
/// @param checker Path to the checker executable.
/// @param input Path to the case input.
/// @param output Path to the contestant's output.
/// @param answer Path to the expected answer.
/// @return The verdict, or an error when the checker could not be started at all.
///
/// The three paths are passed as the three arguments testlib's `registerTestlibCmd` expects, in that
/// order. The verdict is read from standard error because that is where testlib writes it.
[[nodiscard]] Result<Verdict> run_checker(const std::string& checker, const std::string& input,
                                          const std::string& output, const std::string& answer);

/// @brief The spelling this judge reports for a verdict.
[[nodiscard]] std::string_view verdict_name(Verdict verdict);

}  // namespace cms::judge
