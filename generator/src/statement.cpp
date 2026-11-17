#include <cms/generate/statement.hpp>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

#include <simdjson.h>

#include "json.hpp"

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/generate/data_frame.hpp>
#include <cms/object_store.hpp>

namespace cms::generate {
namespace {

using simdjson::dom::object;

struct SectionName {
  std::string_view key;
  std::string_view heading;
};

constexpr std::array<SectionName, 6> SECTIONS{{
    {"task", "Task"},
    {"input", "Input"},
    {"output", "Output"},
    {"constraints", "Constraints"},
    {"example", "Example"},
    {"scoring", "Scoring"},
}};

constexpr std::string_view TRAP_KEY = "trap";

auto refuse(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

auto trimmed(std::string_view text) -> std::string_view {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

template <typename Sections>
auto section_at(Sections& sections, std::size_t index) -> decltype(auto) {
  switch (index) {
    case 0:
      return (sections.task);
    case 1:
      return (sections.input);
    case 2:
      return (sections.output);
    case 3:
      return (sections.constraints);
    case 4:
      return (sections.example);
    default:
      return (sections.scoring);
  }
}

auto constraints_text(const Specification& specification) -> std::string {
  auto text = std::to_string(specification.bounds.min_n) + " <= n <= " +
              std::to_string(specification.bounds.max_n) + "\n";
  text += "each value between " + std::to_string(specification.bounds.value_min) + " and " +
          std::to_string(specification.bounds.value_max) + "\n";
  text += "the smallest case reads n = " + std::to_string(specification.bounds.min_n) + "\n";
  for (const auto& subtask : specification.subtasks) {
    text += "subtask " + std::to_string(subtask.position) + ": " + subtask.constraint + "\n";
  }
  return text;
}

auto framed(std::string_view label, std::string_view text) -> Result<std::string> {
  return frame_data(ModelData{.label = std::string{label}, .text = std::string{text}});
}

/// @return Whether @p text carries a decimal digit, ASCII or Thai, which ADR 0034 §3 keeps out of the example's
/// prose because every number there is machine-derived.
auto has_number(std::string_view text) -> bool {
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto byte = static_cast<unsigned char>(text[index]);
    if (byte >= '0' && byte <= '9') {
      return true;
    }
    if (byte == 0xE0U && index + 2 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0xB9U) {
      const auto third = static_cast<unsigned char>(text[index + 2]);
      if (third >= 0x90U && third <= 0x99U) {
        return true;
      }
    }
  }
  return false;
}

auto example_block(const ExamplePair& example) -> std::string {
  return "Input:\n\n```\n" + example.input + "```\n\nOutput:\n\n```\n" + example.answer + "```\n";
}

auto row_of(const LocalizedStatement& statement, const ExamplePair& example, std::string_view kind)
    -> std::string {
  return kind == TITLE_KIND ? statement.title : render_statement(statement, example);
}

auto artefacts_for(const Step4Answer& answer, const OracleCheck& oracle, const LeakProof& proof)
    -> Result<std::vector<Artefact>> {
  auto missing = std::string{};
  if (!proof.is_brief_checked) {
    missing = "check:brief-leak";
  }
  if (!proof.is_algo_checked) {
    missing += missing.empty() ? "check:algo-leak" : " and check:algo-leak";
  }
  if (!missing.empty()) {
    return std::unexpected(refuse("the statement rows are refused: " + missing +
                                  (missing.find(" and ") == std::string::npos ? " has" : " have") +
                                  " not run over them"));
  }
  if (answer.statements.empty()) {
    return std::unexpected(refuse("the answer carries no statement to record"));
  }
  if (answer.statements.size() != answer.calls.size() ||
      answer.statements.size() != answer.answer_json.size()) {
    return std::unexpected(refuse("the step recorded " + std::to_string(answer.statements.size()) +
                                  " statements, " + std::to_string(answer.calls.size()) + " calls and " +
                                  std::to_string(answer.answer_json.size()) + " answers"));
  }
  if (answer.example.input != oracle.smallest_input || answer.example.answer != oracle.answer) {
    return std::unexpected(refuse("the example is not the bytes the brute-force program printed for the smallest "
                                  "case the bounds allow (ADR 0034 §3)"));
  }
  auto artefacts = std::vector<Artefact>{};
  artefacts.reserve(answer.statements.size() * 2);
  for (std::size_t index = 0; index < answer.statements.size(); ++index) {
    const auto& statement = answer.statements[index];
    if (!is_locale_tag(statement.locale)) {
      return std::unexpected(refuse("a statement is written for a locale whose tag \"" + statement.locale +
                                    "\" is not lowercase letters, digits and dashes"));
    }
    for (std::size_t other = 0; other < index; ++other) {
      if (answer.statements[other].locale == statement.locale) {
        return std::unexpected(refuse("two statements carry the locale \"" + statement.locale +
                                      "\"; one locale is one statement"));
      }
    }
    if (trimmed(statement.title).empty()) {
      return std::unexpected(refuse("the " + statement.locale + " statement carries no title"));
    }
    for (std::size_t section = 0; section < SECTIONS.size(); ++section) {
      const auto& prose = section_at(statement.sections, section);
      if (trimmed(prose).empty()) {
        return std::unexpected(refuse("the " + statement.locale + " statement has a blank section \"" +
                                      std::string{SECTIONS[section].key} + "\""));
      }
      if (SECTIONS[section].key == "example" && has_number(prose)) {
        return std::unexpected(refuse("the " + statement.locale +
                                      " statement's example section carries a number; ADR 0034 §3 makes the "
                                      "example's bytes machine-derived and the prose around it number-free"));
      }
    }
    for (const auto kind : {TITLE_KIND, STATEMENT_KIND}) {
      artefacts.push_back(Artefact{.step = STATEMENT_STEP,
                                   .kind = std::string{kind},
                                   .locale = statement.locale,
                                   .home = ArtefactHome::row,
                                   .object_key = {},
                                   .bytes = row_of(statement, answer.example, kind),
                                   .call = answer.calls[index]});
    }
    artefacts.push_back(Artefact{.step = STATEMENT_STEP,
                                 .kind = std::string{ANSWER_KIND},
                                 .locale = statement.locale,
                                 .home = ArtefactHome::row,
                                 .object_key = {},
                                 .bytes = answer.answer_json[index],
                                 .call = answer.calls[index]});
  }
  return artefacts;
}

auto write_file(const std::filesystem::path& file, std::string_view text) -> Result<void> {
  auto stream = std::ofstream{file, std::ios::trunc | std::ios::binary};
  stream << text;
  if (!stream) {
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       "the leak check's temporary copy could not be written: " + file.string()));
  }
  return {};
}

}  // namespace

auto is_locale_tag(std::string_view locale) -> bool {
  if (locale.empty()) {
    return false;
  }
  for (const auto character : locale) {
    const auto is_lower = character >= 'a' && character <= 'z';
    const auto is_digit = character >= '0' && character <= '9';
    if (!is_lower && !is_digit && character != '-') {
      return false;
    }
  }
  return true;
}

auto render_statement(const LocalizedStatement& statement, const ExamplePair& example) -> std::string {
  auto markdown = "# " + statement.title + "\n";
  for (std::size_t index = 0; index < SECTIONS.size(); ++index) {
    markdown += "\n## " + std::string{SECTIONS[index].heading} + "\n\n";
    markdown += section_at(statement.sections, index);
    markdown += '\n';
    if (SECTIONS[index].key == "example") {
      markdown += '\n';
      markdown += example_block(example);
    }
  }
  return markdown;
}

auto read_statement_request(::drogon::orm::DbClient& client, std::int64_t job_id) -> Result<StatementRequest> {
  try {
    auto transaction = cms::db::Transaction{client};
    auto found = std::optional<StatementRequest>{};
    for (const auto& row : transaction.connection()(
             sqlpp::select(cms::db::generationJobs.brief, cms::db::generationJobs.description)
                 .from(cms::db::generationJobs)
                 .where(cms::db::generationJobs.id == job_id))) {
      found = StatementRequest{.brief = std::string{row.brief}, .description = std::string{row.description}};
    }
    transaction.commit();
    if (!found) {
      return std::unexpected(Error::make(ErrorCode::not_found, "no generation job " + std::to_string(job_id)));
    }
    if (trimmed(found->brief).empty() && trimmed(found->description).empty()) {
      return std::unexpected(refuse("generation job " + std::to_string(job_id) +
                                    " stores no request: brief and description are empty, so there is nothing to "
                                    "compare a statement against"));
    }
    if (trimmed(found->brief).empty()) {
      return std::unexpected(refuse("generation job " + std::to_string(job_id) +
                                    " stores no brief; ADR 0036 §2 writes it when the job is created"));
    }
    if (trimmed(found->description).empty()) {
      return std::unexpected(refuse("generation job " + std::to_string(job_id) +
                                    " stores no description; ADR 0036 §2 writes it when the job is created"));
    }
    return *found;
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

auto trap_sentence(std::string_view plan_json) -> Result<std::string> {
  auto trap = std::string{};
  const auto used = detail::with_object(plan_json, "the chosen plan", [&](const object& plan) -> Result<void> {
    const auto read = detail::field_text(plan, TRAP_KEY);
    if (!read || trimmed(*read).empty()) {
      return std::unexpected(refuse("the chosen plan carries no trap sentence (key \"trap\"); ADR 0036 §3 makes "
                                    "the plan the one place it can come from, so a plan without one is refused "
                                    "rather than guessed"));
    }
    trap = std::string{trimmed(*read)};
    return {};
  });
  if (!used) {
    return std::unexpected(used.error());
  }
  return trap;
}

auto read_technique_names(const std::filesystem::path& vocabulary, std::string_view id) -> Result<TechniqueNames> {
  auto stream = std::ifstream{vocabulary, std::ios::binary};
  if (!stream) {
    return std::unexpected(
        Error::make(ErrorCode::unavailable, "the topic vocabulary cannot be read: " + vocabulary.string()));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  const auto text = buffer.str();
  const auto padded = simdjson::padded_string{text};
  auto parser = simdjson::dom::parser{};
  auto document = simdjson::dom::element{};
  if (parser.parse(padded).get(document) != simdjson::SUCCESS) {
    return std::unexpected(refuse("the topic vocabulary is not valid JSON: " + vocabulary.string()));
  }
  auto topics = simdjson::dom::array{};
  if (document.at_key("topics").get_array().get(topics) != simdjson::SUCCESS) {
    return std::unexpected(refuse("the topic vocabulary carries no topics array: " + vocabulary.string()));
  }
  for (const auto entry : topics) {
    auto topic = simdjson::dom::object{};
    if (entry.get_object().get(topic) != simdjson::SUCCESS) {
      continue;
    }
    const auto topic_id = detail::field_text(topic, "id");
    if (!topic_id || *topic_id != id) {
      continue;
    }
    const auto english = detail::field_text(topic, "en");
    const auto thai = detail::field_text(topic, "th");
    if (!english || !thai) {
      return std::unexpected(refuse("the topic \"" + std::string{id} + "\" is missing its en or th label"));
    }
    return TechniqueNames{.id = *topic_id, .english = *english, .thai = *thai, .acronym = technique_acronym(*english)};
  }
  return std::unexpected(refuse("the topic vocabulary does not define the technique \"" + std::string{id} + "\""));
}

auto statement_prompt(const Specification& specification, std::string_view trap,
                      const LocaleDeclaration& locale) -> Result<std::string> {
  if (!is_locale_tag(locale.tag) || trimmed(locale.name).empty()) {
    return std::unexpected(refuse("step 4 cannot write a statement for the locale \"" + locale.tag +
                                  "\": a locale needs a tag and the language's own name"));
  }
  if (trimmed(trap).empty()) {
    return std::unexpected(refuse("step 4 was handed no trap sentence to describe (ADR 0036 §3)"));
  }
  const auto input = framed("the input", specification.input);
  const auto output = framed("the output", specification.output);
  const auto constraints = framed("the constraints", constraints_text(specification));
  const auto shape = framed("what the reader is meant to believe", trap);
  for (const auto* block : {&input, &output, &constraints, &shape}) {
    if (!*block) {
      return std::unexpected(block->error());
    }
  }
  auto prompt = std::string{"You are step 4 of a task-generation pipeline. You write the statement of one "
                            "programming task for a school judge, in "};
  prompt += locale.name;
  prompt +=
      ".\n"
      "You are not told the intended algorithm, and the statement must not name one, or hint at one by its "
      "common name. The reader is meant to believe what the last block below describes; write so that belief is "
      "natural, and let a careful reader find the way out from the constraints alone.\n\n"
      "The facts of the task are below as data. Read them; do not copy them word for word.\n\n";
  prompt += *input + "\n" + *output + "\n" + *constraints + "\n" + *shape;
  prompt +=
      "\nAnswer with one JSON object and nothing else. It carries:\n"
      "  \"title\": the page heading, one line;\n"
      "  \"task\": the section that says what is given and what is asked;\n"
      "  \"input\": the section that says what the program reads;\n"
      "  \"output\": the section that says what it prints;\n"
      "  \"constraints\": the section that states the bounds;\n"
      "  \"example\": one or two sentences that introduce the worked example, with no number in them;\n"
      "  \"scoring\": the section that says how the subtasks are scored.\n"
      "Every section is prose in ";
  prompt += locale.name;
  prompt += "; the worked example's input and answer are supplied by the pipeline, so never write them.\n";
  return prompt;
}

auto parse_statement_answer(std::string_view answer, std::string_view locale, const ModelCall&)
    -> Result<LocalizedStatement> {
  const auto what = "the " + std::string{locale} + " statement answer";
  auto title = std::string{};
  auto values = std::vector<std::string>(SECTIONS.size());
  auto missing = std::vector<std::string>{};
  const auto used = detail::with_object(answer, what, [&](const object& body) -> Result<void> {
    const auto read_title = detail::field_text(body, "title");
    if (!read_title || trimmed(*read_title).empty()) {
      missing.emplace_back("title");
    } else {
      title = *read_title;
    }
    for (std::size_t index = 0; index < SECTIONS.size(); ++index) {
      const auto read = detail::field_text(body, SECTIONS[index].key);
      if (!read || trimmed(*read).empty()) {
        missing.emplace_back(SECTIONS[index].key);
        continue;
      }
      values[index] = *read;
    }
    if (!missing.empty()) {
      auto names = std::string{};
      for (std::size_t index = 0; index < missing.size(); ++index) {
        names += index == 0 ? "" : ", ";
        names += "\"" + missing[index] + "\"";
      }
      return std::unexpected(refuse(what + " is missing " + names +
                                    "; the six sections ADR 0031 fixes are never padded"));
    }
    return {};
  });
  if (!used) {
    return std::unexpected(used.error());
  }
  auto parsed = LocalizedStatement{.locale = std::string{locale}, .title = std::move(title), .sections = {}};
  for (std::size_t index = 0; index < SECTIONS.size(); ++index) {
    section_at(parsed.sections, index) = std::move(values[index]);
  }
  return parsed;
}

auto write_statement(ModelClient& client, const StatementInput& input, const OracleCheck& oracle_check)
    -> Result<Step4Answer> {
  if (input.locales.empty()) {
    return std::unexpected(refuse("step 4 was asked for no locale; a task ships at least one"));
  }
  auto has_english = false;
  for (std::size_t index = 0; index < input.locales.size(); ++index) {
    const auto& declaration = input.locales[index];
    if (!is_locale_tag(declaration.tag) || trimmed(declaration.name).empty()) {
      return std::unexpected(refuse("step 4 was handed the locale \"" + declaration.tag +
                                    "\" without a tag and the language's own name"));
    }
    if (declaration.tag == ENGLISH_LOCALE) {
      has_english = true;
    }
    for (std::size_t other = 0; other < index; ++other) {
      if (input.locales[other].tag == declaration.tag) {
        return std::unexpected(refuse("step 4 was asked for the locale \"" + declaration.tag +
                                      "\" twice; one locale is one statement"));
      }
    }
  }
  if (!has_english) {
    return std::unexpected(refuse("the declared locales carry no \"" + std::string{ENGLISH_LOCALE} +
                                  "\"; ADR 0014 makes English the one language a task always ships"));
  }
  if (trimmed(oracle_check.smallest_input).empty() || trimmed(oracle_check.answer).empty()) {
    return std::unexpected(refuse("the oracle check carries no smallest case or no answer to show as the example"));
  }
  const auto smallest = sample_smallest(input.specification);
  if (!smallest) {
    return std::unexpected(smallest.error());
  }
  if (*smallest != oracle_check.smallest_input) {
    return std::unexpected(refuse("the oracle was checked on a case the bounds do not name: the bounds sample \"" +
                                  *smallest + "\", the check ran on \"" + oracle_check.smallest_input + "\""));
  }
  if (const auto named = named_technique(input.trap, input.technique)) {
    return std::unexpected(refuse("the trap sentence names the technique \"" + *named +
                                  "\"; a plan whose trap cannot be described without the name is refused rather "
                                  "than guessed (ADR 0036 §3)"));
  }
  auto answer = Step4Answer{.statements = {},
                            .example = ExamplePair{.input = oracle_check.smallest_input, .answer = oracle_check.answer},
                            .answer_json = {},
                            .calls = {}};
  for (const auto& declaration : input.locales) {
    const auto prompt = statement_prompt(input.specification, input.trap, declaration);
    if (!prompt) {
      return std::unexpected(prompt.error());
    }
    const auto completion = client.complete(*prompt);
    if (!completion) {
      return std::unexpected(completion.error());
    }
    auto statement = parse_statement_answer(completion->text, declaration.tag, completion->call);
    if (!statement) {
      return std::unexpected(statement.error());
    }
    answer.statements.push_back(std::move(*statement));
    answer.answer_json.push_back(completion->text);
    answer.calls.push_back(completion->call);
  }
  return answer;
}

auto MemoryStatementSink::record(std::int64_t job_id, const Step4Answer& answer, const OracleCheck& oracle,
                                 const LeakProof& proof) -> Result<std::size_t> {
  const auto artefacts = artefacts_for(answer, oracle, proof);
  if (!artefacts) {
    return std::unexpected(artefacts.error());
  }
  job_id_ = job_id;
  rows_.clear();
  for (const auto& artefact : *artefacts) {
    rows_.push_back(RecordedStatement{.step = artefact.step,
                                      .kind = artefact.kind,
                                      .locale = artefact.locale,
                                      .bytes = artefact.bytes,
                                      .call = artefact.call});
  }
  return rows_.size();
}

SqlStatementSink::SqlStatementSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store)
    : client_(client), store_(store) {}

auto SqlStatementSink::record(std::int64_t job_id, const Step4Answer& answer, const OracleCheck& oracle,
                              const LeakProof& proof) -> Result<std::size_t> {
  const auto artefacts = artefacts_for(answer, oracle, proof);
  if (!artefacts) {
    return std::unexpected(artefacts.error());
  }
  auto sink = SqlArtefactSink{client_, store_};
  const auto written = sink.record(job_id, *artefacts);
  if (!written) {
    return std::unexpected(written.error());
  }
  for (const auto& statement : answer.statements) {
    const auto back = read_artefact(client_, store_, job_id, STATEMENT_STEP, STATEMENT_KIND, statement.locale);
    if (!back) {
      return std::unexpected(back.error());
    }
    if (back->bytes != render_statement(statement, answer.example)) {
      return std::unexpected(Error::make(ErrorCode::conflict,
                                         "the " + statement.locale +
                                             " statement read back is not the statement that was written"));
    }
  }
  return *written;
}

LeakWorkspace::LeakWorkspace(std::filesystem::path directory) : directory_(std::move(directory)) {}

LeakWorkspace::LeakWorkspace(LeakWorkspace&& other) noexcept : directory_(std::move(other.directory_)) {
  other.directory_.clear();
}

auto LeakWorkspace::operator=(LeakWorkspace&& other) noexcept -> LeakWorkspace& {
  if (this != &other) {
    remove();
    directory_ = std::move(other.directory_);
    other.directory_.clear();
  }
  return *this;
}

LeakWorkspace::~LeakWorkspace() {
  remove();
}

auto LeakWorkspace::create() -> Result<LeakWorkspace> {
  auto error = std::error_code{};
  const auto base = std::filesystem::temp_directory_path(error);
  if (error) {
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       "the leak check's temporary directory cannot be found: " + error.message()));
  }
  const auto directory =
      base / ("cms-leak-" + std::to_string(static_cast<long>(getpid())) + "-" +
              std::to_string(std::filesystem::file_time_type::clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory, error);
  if (error) {
    return std::unexpected(Error::make(ErrorCode::unavailable,
                                       "the leak check's temporary directory cannot be made: " + error.message()));
  }
  return LeakWorkspace{directory};
}

auto LeakWorkspace::write(const StatementRequest& request) -> Result<void> {
  if (trimmed(request.brief).empty()) {
    return std::unexpected(
        refuse("the request carries no brief, so the leak check has nothing to compare against"));
  }
  if (trimmed(request.description).empty()) {
    return std::unexpected(
        refuse("the request carries no description, so the leak check has nothing to compare against"));
  }
  const auto brief = write_file(directory_ / leak_file::BRIEF, request.brief);
  if (!brief) {
    return brief;
  }
  return write_file(directory_ / leak_file::DESCRIPTION, request.description);
}

auto LeakWorkspace::write(const PlanTexts& plan) -> Result<void> {
  const std::array<std::pair<std::string_view, const std::string*>, 4> files{{
      {leak_file::PLAN_APPROACH, &plan.approach},
      {leak_file::PLAN_COMPLEXITY, &plan.intended_complexity},
      {leak_file::PLAN_BODY, &plan.plan},
      {leak_file::PLAN_REASON, &plan.reason},
  }};
  for (const auto& [name, text] : files) {
    const auto written = write_file(directory_ / name, *text);
    if (!written) {
      return written;
    }
  }
  return {};
}

auto LeakWorkspace::write(const TechniqueNames& technique) -> Result<void> {
  if (technique.id.empty()) {
    return std::unexpected(refuse("the technique the plan names is empty, so the algorithm check cannot run"));
  }
  return write_file(directory_ / leak_file::TECHNIQUE, technique.id);
}

auto LeakWorkspace::write(const Step4Answer& answer) -> Result<void> {
  auto locales = std::string{};
  for (const auto& statement : answer.statements) {
    locales += statement.locale + "\n";
  }
  const auto written_locales = write_file(directory_ / leak_file::LOCALES, locales);
  if (!written_locales) {
    return written_locales;
  }
  for (const auto& statement : answer.statements) {
    const auto title = write_file(directory_ / title_file(statement.locale), statement.title);
    if (!title) {
      return title;
    }
    const auto written =
        write_file(directory_ / statement_file(statement.locale), render_statement(statement, answer.example));
    if (!written) {
      return written;
    }
  }
  return {};
}

auto LeakWorkspace::path(std::string_view name) const -> std::string {
  return (directory_ / name).string();
}

auto LeakWorkspace::remove() -> void {
  if (directory_.empty()) {
    return;
  }
  auto error = std::error_code{};
  std::filesystem::remove_all(directory_, error);
  directory_.clear();
}

}  // namespace cms::generate
