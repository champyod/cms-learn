#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cms/error.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/specification.hpp>

namespace drogon::orm {
class DbClient;
}

namespace cms::store {
class ObjectStore;
}

namespace cms::generate {

/// The one locale the authoring record requires (ADR 0014: English always). Every other locale is optional and
/// symmetric, so it is data the caller declares, never a constant here.
inline constexpr std::string_view ENGLISH_LOCALE = "en";

/// The step the statement artefacts are recorded under (ADR 0015).
inline constexpr std::int16_t STATEMENT_STEP = 4;

/// The kinds the rows per locale carry: the page heading, the statement a student reads, and the answer the model
/// actually sent, kept beside them so a dispute can read it (ADR 0030 §2).
inline constexpr std::string_view TITLE_KIND = "title";
inline constexpr std::string_view STATEMENT_KIND = "statement";
inline constexpr std::string_view ANSWER_KIND = "answer";

/// @brief The prose of each fixed section, as the model wrote it. The example's bytes are not here: no model
/// writes a number ADR 0034 makes machine-derived.
struct StatementSections {
  std::string task;
  std::string input;
  std::string output;
  std::string constraints;
  std::string example;
  std::string scoring;
};

/// @brief One locale's statement: the title, the prose of each section, and the locale it is written for.
struct LocalizedStatement {
  std::string locale;
  std::string title;
  StatementSections sections;
};

/// @brief The one example both locales show: the smallest case the bounds allow and the answer the brute-force
/// program printed for it (ADR 0034 §3).
struct ExamplePair {
  std::string input;
  std::string answer;
};

/// @brief What step 4 produced: one statement per locale, the example, the answers as they arrived and their calls.
struct Step4Answer {
  std::vector<LocalizedStatement> statements;
  ExamplePair example;
  std::vector<std::string> answer_json;
  std::vector<ModelCall> calls;
};

/// @brief One locale the task ships: the tag its rows carry, and the language's own name for the prompt.
///
/// The name is data because a prompt that names a language is the only place one is needed; no function, constant
/// or branch in this step knows a language, so a locale the authoring surface adds later needs no code.
struct LocaleDeclaration {
  std::string tag;
  std::string name;
};

/// @brief The teacher's request, as the job row stores it (ADR 0036 §2).
struct StatementRequest {
  std::string brief;
  std::string description;
};

/// @brief The four texts the chosen candidate row holds, which the brief check compares against (ADR 0037 §3).
struct PlanTexts {
  std::string approach;
  std::string intended_complexity;
  std::string plan;
  std::string reason;
};

/// @brief The four names one technique is matchable by: its topic id, both labels and the acronym.
struct TechniqueNames {
  std::string id;
  std::string english;
  std::string thai;
  std::string acronym;
};

/// @brief What the caller proved before either locale may be written.
struct LeakProof {
  bool is_brief_checked{false};
  bool is_algo_checked{false};
};

/// @brief One locale's title and statement, which are the two surfaces a student reads.
struct LeakSubject {
  std::string locale;
  std::string title;
  std::string statement;
};

/// @brief The file names the check's temporary copy holds, so the caller and the check agree on one interface
/// without a shared constant table (ADR 0035 §2).
namespace leak_file {
inline constexpr std::string_view BRIEF = "brief.txt";
inline constexpr std::string_view DESCRIPTION = "description.txt";
inline constexpr std::string_view PLAN_APPROACH = "plan-approach.txt";
inline constexpr std::string_view PLAN_COMPLEXITY = "plan-complexity.txt";
inline constexpr std::string_view PLAN_BODY = "plan.txt";
inline constexpr std::string_view PLAN_REASON = "plan-reason.txt";
inline constexpr std::string_view TECHNIQUE = "technique.txt";
inline constexpr std::string_view LOCALES = "locales.txt";
}  // namespace leak_file

/// @return The file one locale's title is written to in the check's temporary copy.
[[nodiscard]] inline auto title_file(std::string_view locale) -> std::string {
  return "title." + std::string{locale} + ".txt";
}

/// @return The file one locale's statement is written to in the check's temporary copy.
[[nodiscard]] inline auto statement_file(std::string_view locale) -> std::string {
  return "statement." + std::string{locale} + ".txt";
}

/// @return Whether @p locale has the shape of a language tag: lowercase letters, digits and dashes, non-empty.
[[nodiscard]] auto is_locale_tag(std::string_view locale) -> bool;

/// @brief Renders one locale's markdown: the title, then the six sections in the order ADR 0031 names them, with the
/// example's bytes taken from @p example rather than from the answer.
[[nodiscard]] auto render_statement(const LocalizedStatement& statement, const ExamplePair& example) -> std::string;

/// @brief Reads the request a job row stores, refusing an empty one by name (ADR 0036 §4).
[[nodiscard]] auto read_statement_request(::drogon::orm::DbClient& client, std::int64_t job_id)
    -> Result<StatementRequest>;

/// @brief Reads the trap sentence the chosen plan carries, refusing a plan that has none (ADR 0036 §3).
[[nodiscard]] auto trap_sentence(std::string_view plan_json) -> Result<std::string>;

/// @brief Resolves one technique's four names from the vocabulary, refusing an id the vocabulary does not define.
[[nodiscard]] auto read_technique_names(const std::filesystem::path& vocabulary, std::string_view id)
    -> Result<TechniqueNames>;

/// @brief The prompt step 4 sends for one locale: step 3's input, output and constraints, and the trap's shape.
///
/// The request, the plan, the approach and the technique are not parameters, so no prompt can carry them.
[[nodiscard]] auto statement_prompt(const Specification& specification, std::string_view trap,
                                    const LocaleDeclaration& locale) -> Result<std::string>;

/// @brief Reads one locale's answer: its title and the six sections, each named when it is missing or blank.
[[nodiscard]] auto parse_statement_answer(std::string_view answer, std::string_view locale,
                                          const ModelCall& call) -> Result<LocalizedStatement>;

/// @brief The facts step 4 writes from: the specification, the trap and the technique, for the locales it ships.
///
/// The oracle's bytes arrive with the oracle check, and the limits are step 3's: a field this step never reads is
/// a claim nobody checks, so neither is a member here.
struct StatementInput {
  Specification specification;
  std::string trap;
  TechniqueNames technique;
  std::vector<LocaleDeclaration> locales;
};

/// @brief Runs step 4: one call per locale, the example taken from @p oracle_check, and both statements rendered.
///
/// @return The answer, or a refusal naming the fact: a locale the task does not ship, a trap that names the
/// technique, an answer missing a section, or an oracle whose bytes are not the example's.
[[nodiscard]] auto write_statement(ModelClient& client, const StatementInput& input,
                                   const OracleCheck& oracle_check) -> Result<Step4Answer>;

/// @brief One row a statement write leaves, as a test reads it back without a database.
struct RecordedStatement {
  std::int16_t step{0};
  std::string kind;
  std::string locale;
  std::string bytes;
  ModelCall call;
};

/// @brief Where step 4's rows go, once both leak checks have run over what it wrote.
class StatementSink {
 public:
  virtual ~StatementSink() = default;

  /// @param job_id The generation job the rows belong to.
  /// @param answer What step 4 produced, already rendered.
  /// @param oracle The oracle's own check, whose bytes the example must be.
  /// @param proof Whether both leak checks passed before this write.
  /// @return How many rows were written, or the reason the write stopped.
  [[nodiscard]] virtual auto record(std::int64_t job_id, const Step4Answer& answer, const OracleCheck& oracle,
                                    const LeakProof& proof) -> Result<std::size_t> = 0;
};

/// @brief A sink that keeps what a step would have written, so a test needs no database.
class MemoryStatementSink final : public StatementSink {
 public:
  [[nodiscard]] auto record(std::int64_t job_id, const Step4Answer& answer, const OracleCheck& oracle,
                            const LeakProof& proof) -> Result<std::size_t> override;

  [[nodiscard]] auto job_id() const -> std::int64_t { return job_id_; }
  [[nodiscard]] auto rows() const -> const std::vector<RecordedStatement>& { return rows_; }

 private:
  std::int64_t job_id_{0};
  std::vector<RecordedStatement> rows_;
};

/// @brief The sink the worker uses: both locales through the one public artefact write, then read back per locale.
class SqlStatementSink final : public StatementSink {
 public:
  SqlStatementSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store);

  [[nodiscard]] auto record(std::int64_t job_id, const Step4Answer& answer, const OracleCheck& oracle,
                            const LeakProof& proof) -> Result<std::size_t> override;

 private:
  ::drogon::orm::DbClient& client_;
  cms::store::ObjectStore& store_;
};

/// @brief The temporary directory the caller owns and removes: the request, the plan and the written statements the
/// checks read, and the only copy of the request in the run (ADR 0035 §2, ADR 0037 §4).
class LeakWorkspace {
 public:
  /// @return A directory of this process's own under the temporary directory, or the reason it could not be made.
  [[nodiscard]] static auto create() -> Result<LeakWorkspace>;

  LeakWorkspace(const LeakWorkspace&) = delete;
  auto operator=(const LeakWorkspace&) -> LeakWorkspace& = delete;
  LeakWorkspace(LeakWorkspace&& other) noexcept;
  auto operator=(LeakWorkspace&& other) noexcept -> LeakWorkspace&;
  ~LeakWorkspace();

  /// @brief Writes the request as two files, refusing to write one when it is empty (ADR 0036 §4).
  [[nodiscard]] auto write(const StatementRequest& request) -> Result<void>;

  /// @brief Writes the four plan texts as four files.
  [[nodiscard]] auto write(const PlanTexts& plan) -> Result<void>;

  /// @brief Writes the technique id, which the algorithm check resolves against the vocabulary.
  [[nodiscard]] auto write(const TechniqueNames& technique) -> Result<void>;

  /// @brief Writes the declared locale set and each locale's title and statement as the check receives them.
  [[nodiscard]] auto write(const Step4Answer& answer) -> Result<void>;

  /// @return The file inside the directory a name from `leak_file` or a generated locale name refers to.
  [[nodiscard]] auto path(std::string_view name) const -> std::string;

  [[nodiscard]] auto directory() const -> const std::filesystem::path& { return directory_; }

  /// @brief Removes the directory and everything in it; safe to call twice, and the destructor calls it.
  auto remove() -> void;

 private:
  explicit LeakWorkspace(std::filesystem::path directory);

  std::filesystem::path directory_;
};

/// @brief Refuses @p subject when it repeats eight English words or thirty-two Thai characters of @p request or
/// @p plan, quoting the subject's own span and never the source's (ADR 0037 §3).
///
/// @return Nothing when nothing is repeated, or the refusal naming the source and quoting the subject's span.
[[nodiscard]] auto check_brief_leak(const std::vector<LeakSubject>& subjects, const StatementRequest& request,
                                    const PlanTexts& plan) -> Result<void>;

/// @brief Refuses any subject in @p subjects when it names @p technique by id, any label or acronym (ADR 0036 §3).
[[nodiscard]] auto check_algo_leak(const std::vector<LeakSubject>& subjects, const TechniqueNames& technique)
    -> Result<void>;

/// @return The acronym one English label is matchable by: the initial letters of its words, or empty when the label
/// is one word and its initial is no name.
[[nodiscard]] auto technique_acronym(std::string_view english) -> std::string;

/// @return The name @p text contains, or nothing when it names no technique; shared by the check and the trap guard.
[[nodiscard]] auto named_technique(std::string_view text, const TechniqueNames& technique)
    -> std::optional<std::string>;

/// @brief The check program the two shell gates run: `brief` compares against the request, `algo` against a name.
/// @return 0 when the text is clean, 1 when it leaks, 64 when the command line is not this program's.
[[nodiscard]] auto leak_command(int argc, char** argv) -> int;

}  // namespace cms::generate
