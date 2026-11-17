// Step 4 turns step 3's specification into a task's title and statement in each locale it ships, with the worked
// example taken from the stored brute-force program and both leak checks standing between the answer and a row.
// The tests that need a database are tagged [live] and skip when CMSLEAN_TEST_DSN is unset.

#include <cms/generate/candidate.hpp>
#include <cms/generate/claim.hpp>
#include <cms/generate/model.hpp>
#include <cms/generate/specification.hpp>
#include <cms/generate/statement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/generate/artefact.hpp>
#include <cms/object_store.hpp>

#include "live_database.hpp"
#include "memory_store.hpp"
#include "pipeline_fixture.hpp"

namespace {

using cms::generate::check_algo_leak;
using cms::generate::check_brief_leak;
using cms::generate::ExamplePair;
using cms::generate::LeakProof;
using cms::generate::LeakSubject;
using cms::generate::LeakWorkspace;
using cms::generate::LocalizedStatement;
using cms::generate::MemoryStatementSink;
using cms::generate::ModelCall;
using cms::generate::OracleCheck;
using cms::generate::PlanTexts;
using cms::generate::Specification;
using cms::generate::StatementInput;
using cms::generate::StatementRequest;
using cms::generate::StatementSections;
using cms::generate::Step4Answer;
using cms::generate::TechniqueNames;

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

constexpr std::string_view SMALLEST = "2\n-3 -3\n";

SQLPP_CREATE_NAME_TAG(row_count);
constexpr std::string_view THAI_BRIEF =
    "กำหนดอาร์เรย์ของจำนวนเต็มแล้วตอบคำถามเกี่ยวกับผลรวมของช่วงที่กำหนดให้ทุกคำถาม";

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

auto section_prose(std::string_view locale, std::string_view key) -> std::string {
  const auto thai = std::string_view{locale} == "th";
  if (key == "task") {
    return thai ? "กำหนดจำนวนเต็ม n จำนวน และตอบผลรวมของช่วงที่ถาม" : "You are given n integers and asked for range sums.";
  }
  if (key == "input") {
    return thai ? "อ่านค่า n แล้วตามด้วยจำนวนเต็ม n จำนวน" : "Read n, then n integers.";
  }
  if (key == "output") {
    return thai ? "พิมพ์คำตอบของแต่ละคำถาม" : "Print the answer to each query.";
  }
  if (key == "constraints") {
    return thai ? "จำนวนเต็มทุกค่าอยู่ในช่วงที่กำหนด" : "Every value stays inside the declared bounds.";
  }
  if (key == "example") {
    return thai ? "ตัวอย่างด้านล่างแสดงกรณีเล็กที่สุด" : "The example below shows the smallest case.";
  }
  return thai ? "แต่ละงานย่อยได้คะแนนตามที่กำหนด" : "Each subtask carries the points it declares.";
}

auto answer_json(std::string_view locale, std::string_view omit = {}, std::string_view task = {}) -> std::string {
  auto json = std::string{"{"};
  json += "\"title\": " + json_string(std::string_view{locale} == "th" ? "ผลรวมของช่วงข้อมูล" : "Range Sum Queries");
  for (const auto key : {"task", "input", "output", "constraints", "example", "scoring"}) {
    if (std::string_view{key} == omit) {
      continue;
    }
    const auto prose = std::string_view{key} == "task" && !task.empty() ? std::string{task}
                                                                          : section_prose(locale, key);
    json += ", " + json_string(key) + ": " + json_string(prose);
  }
  json += "}";
  return json;
}

auto specification() -> Specification {
  auto spec = Specification{};
  spec.input = "n, then n integers";
  spec.output = "their sum";
  spec.bounds = cms::generate::Bounds{.min_n = 2, .max_n = 5, .value_min = -3, .value_max = 7};
  spec.subtasks = {{.position = 1, .constraint = "n <= 3"}};
  return spec;
}

auto oracle_check() -> const OracleCheck& {
  static const auto checked = [] {
    const auto result = cms::generate::check_oracle(SUM_ORACLE, SMALLEST, 1000);
    REQUIRE(result.has_value());
    return *result;
  }();
  return checked;
}

auto english() -> cms::generate::LocaleDeclaration {
  return cms::generate::LocaleDeclaration{.tag = "en", .name = "English"};
}

auto prefix_sums() -> TechniqueNames {
  return TechniqueNames{.id = "prefix-sums", .english = "Prefix sums", .thai = "ผลรวมสะสม", .acronym = "ps"};
}

auto statement_input(std::string trap = "the reader is meant to believe a scan of every pair is fast enough")
    -> StatementInput {
  return StatementInput{.specification = specification(),
                        .trap = std::move(trap),
                        .technique = prefix_sums(),
                        .locales = {cms::generate::LocaleDeclaration{.tag = "en", .name = "English"},
                                    cms::generate::LocaleDeclaration{.tag = "th", .name = "Thai"}}};
}

auto recorded(std::vector<std::string> answers) -> cms::generate::RecordedClient {
  return cms::generate::RecordedClient{"recorded-model", "statement-v1", 17, std::move(answers)};
}

auto plan_texts() -> PlanTexts {
  return PlanTexts{.approach = "prefix sums",
                   .intended_complexity = "O(n)",
                   .plan = R"({"approach": "prefix sums", "n_max": 5})",
                   .reason = "the plan is worth building because it fits the declared bounds"};
}

auto clean_subject(std::string_view locale = "en") -> LeakSubject {
  return LeakSubject{.locale = std::string{locale},
                     .title = "Range Sum Queries",
                     .statement = "# Range Sum Queries\n\nYou are given n integers and a list of ranges."};
}

/// @return The first @p count UTF-8 characters of @p text, so a case can pin the Thai threshold exactly.
auto first_characters(std::string_view text, std::size_t count) -> std::string {
  auto characters = std::size_t{0};
  for (std::size_t index = 0; index < text.size(); ++index) {
    const auto byte = static_cast<unsigned char>(text[index]);
    if ((byte & 0xC0U) == 0x80U) {
      continue;
    }
    if (characters == count) {
      return std::string{text.substr(0, index)};
    }
    ++characters;
  }
  return std::string{text};
}

auto job_state(cms::db::connection& database, std::int64_t job) -> std::string {
  auto state = std::string{};
  for (const auto& row : database(sqlpp::select(cms::db::generationJobs.state)
                                      .from(cms::db::generationJobs)
                                      .where(cms::db::generationJobs.id == job))) {
    state = std::string{row.state};
  }
  return state;
}

auto artefact_rows(cms::db::connection& database, std::int64_t job) -> std::int64_t {
  auto count = std::int64_t{0};
  const auto one = sqlpp::value(1);
  for (const auto& row : database(sqlpp::select(sqlpp::count(one).as(row_count))
                                      .from(cms::db::generationArtefacts)
                                      .where(cms::db::generationArtefacts.jobId == job))) {
    count = row.row_count;
  }
  return count;
}

void store_request(cms::db::connection& database, std::int64_t job, std::string_view brief,
                   std::string_view description) {
  database.execute_text("UPDATE generation_jobs SET brief = $1, description = $2 WHERE id = $3",
                        {std::string{brief}, std::string{description}, job});
}

}  // namespace

TEST_CASE("a recorded answer renders both locales with the six sections in the order 0031 names", "[generate]") {
  auto client = recorded({answer_json("en"), answer_json("th")});

  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());

  REQUIRE(written.has_value());
  REQUIRE(written->statements.size() == 2);
  CHECK(written->statements[0].locale == "en");
  CHECK(written->statements[1].locale == "th");
  CHECK(written->calls[0].call_index == 0);
  CHECK(written->calls[1].call_index == 1);
  CHECK(written->example.input == oracle_check().smallest_input);
  CHECK(written->example.answer == oracle_check().answer);

  const auto markdown = cms::generate::render_statement(written->statements[0], written->example);
  const auto headings = std::vector<std::string>{"# Range Sum Queries", "## Task", "## Input", "## Output",
                                                 "## Constraints", "## Example", "## Scoring"};
  auto position = std::size_t{0};
  for (const auto& heading : headings) {
    const auto found = markdown.find(heading, position);
    REQUIRE(found != std::string::npos);
    position = found;
  }
  CHECK(markdown.find(oracle_check().answer) != std::string::npos);

  auto sink = MemoryStatementSink{};
  const auto rows = sink.record(7, *written, oracle_check(),
                                LeakProof{.is_brief_checked = true, .is_algo_checked = true});
  REQUIRE(rows.has_value());
  CHECK(*rows == 6);
  CHECK(sink.job_id() == 7);
  CHECK(sink.rows()[0].locale == "en");
  CHECK(sink.rows()[0].kind == cms::generate::TITLE_KIND);
  CHECK(sink.rows()[1].locale == "en");
  CHECK(sink.rows()[1].kind == cms::generate::STATEMENT_KIND);
  CHECK(sink.rows()[2].locale == "en");
  CHECK(sink.rows()[2].kind == cms::generate::ANSWER_KIND);
  CHECK(sink.rows()[3].locale == "th");
  CHECK(sink.rows()[3].kind == cms::generate::TITLE_KIND);
  CHECK(sink.rows()[5].locale == "th");
  CHECK(sink.rows()[5].kind == cms::generate::ANSWER_KIND);
}

TEST_CASE("an answer missing a section is refused by name and never padded", "[generate]") {
  auto client = recorded({answer_json("en"), answer_json("th", "scoring")});

  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());

  REQUIRE(!written.has_value());
  CHECK(written.error().message.find("\"scoring\"") != std::string::npos);
  CHECK(written.error().message.find("th") != std::string::npos);
  CHECK(written.error().message.find("never padded") != std::string::npos);
}

TEST_CASE("the example is the oracle's bytes, and a changed digit is refused by name", "[generate]") {
  auto client = recorded({answer_json("en"), answer_json("th")});
  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());
  REQUIRE(written.has_value());

  auto tampered = *written;
  const auto digit = tampered.example.answer.find_last_of("0123456789");
  REQUIRE(digit != std::string::npos);
  tampered.example.answer[digit] = tampered.example.answer[digit] == '9' ? '8' : '9';

  auto sink = MemoryStatementSink{};
  const auto refused = sink.record(7, tampered, oracle_check(),
                                   LeakProof{.is_brief_checked = true, .is_algo_checked = true});

  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("example") != std::string::npos);
  CHECK(refused.error().message.find("brute-force program") != std::string::npos);
  CHECK(sink.rows().empty());
}

TEST_CASE("the constraints line is the bounds' own, and the smallest case is stated once", "[generate]") {
  auto spec = specification();
  spec.bounds.min_n = 3;
  spec.bounds.max_n = 5;

  const auto prompt = cms::generate::statement_prompt(spec, "the answer looks cheap", english());

  REQUIRE(prompt.has_value());
  CHECK(prompt->find("3 <= n <= 5") != std::string::npos);
  CHECK(prompt->find("1 <= n") == std::string::npos);
  CHECK(prompt->find("the smallest case reads n = 3") != std::string::npos);
  CHECK(prompt->find("the smallest case reads n = 1") == std::string::npos);
}

TEST_CASE("a write is refused until both checks have run, naming the one that has not", "[generate]") {
  auto client = recorded({answer_json("en"), answer_json("th")});
  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());
  REQUIRE(written.has_value());

  auto neither_sink = MemoryStatementSink{};
  const auto neither = neither_sink.record(9, *written, oracle_check(), LeakProof{});
  REQUIRE(!neither.has_value());
  CHECK(neither.error().message.find("check:brief-leak") != std::string::npos);
  CHECK(neither.error().message.find("check:algo-leak") != std::string::npos);
  CHECK(neither_sink.rows().empty());

  auto without_brief = MemoryStatementSink{};
  const auto brief_refused = without_brief.record(
      9, *written, oracle_check(), LeakProof{.is_brief_checked = false, .is_algo_checked = true});
  REQUIRE(!brief_refused.has_value());
  CHECK(brief_refused.error().message.find("check:brief-leak") != std::string::npos);
  CHECK(brief_refused.error().message.find("check:algo-leak") == std::string::npos);
  CHECK(without_brief.rows().empty());

  auto without_algo = MemoryStatementSink{};
  const auto algo_refused = without_algo.record(
      9, *written, oracle_check(), LeakProof{.is_brief_checked = true, .is_algo_checked = false});
  REQUIRE(!algo_refused.has_value());
  CHECK(algo_refused.error().message.find("check:algo-leak") != std::string::npos);
  CHECK(algo_refused.error().message.find("check:brief-leak") == std::string::npos);
  CHECK(without_algo.rows().empty());

  auto both = MemoryStatementSink{};
  REQUIRE(both.record(9, *written, oracle_check(), LeakProof{.is_brief_checked = true, .is_algo_checked = true})
              .has_value());
  CHECK(both.rows().size() == 6);
}

TEST_CASE("an example section carrying a number is refused, since ADR 0034 derives every number", "[generate]") {
  auto client = recorded({answer_json("en"), answer_json("th")});
  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());
  REQUIRE(written.has_value());

  auto numbered = *written;
  numbered.statements[0].sections.example = "For example, the input below sums 3 numbers.";
  auto sink = MemoryStatementSink{};
  const auto refused =
      sink.record(9, numbered, oracle_check(), LeakProof{.is_brief_checked = true, .is_algo_checked = true});
  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("example section carries a number") != std::string::npos);
  CHECK(refused.error().message.find("en") != std::string::npos);
  CHECK(sink.rows().empty());

  auto thai_numbered = *written;
  thai_numbered.statements[1].sections.example = "ตัวอย่างนี้มี ๓ จำนวน";
  auto thai_sink = MemoryStatementSink{};
  const auto thai_refused =
      thai_sink.record(9, thai_numbered, oracle_check(), LeakProof{.is_brief_checked = true, .is_algo_checked = true});
  REQUIRE(!thai_refused.has_value());
  CHECK(thai_refused.error().message.find("number") != std::string::npos);
}

TEST_CASE("the model's answer is recorded beside the rendered rows", "[generate]") {
  auto client = recorded({answer_json("en"), answer_json("th")});
  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());
  REQUIRE(written.has_value());

  auto sink = MemoryStatementSink{};
  REQUIRE(sink.record(11, *written, oracle_check(), LeakProof{.is_brief_checked = true, .is_algo_checked = true})
              .has_value());
  REQUIRE(sink.rows().size() == 6);

  const auto& answer_row = sink.rows()[2];
  CHECK(answer_row.kind == cms::generate::ANSWER_KIND);
  CHECK(answer_row.locale == "en");
  CHECK(answer_row.bytes == written->answer_json[0]);
  CHECK(sink.rows()[5].kind == cms::generate::ANSWER_KIND);
  CHECK(sink.rows()[5].locale == "th");
  CHECK(sink.rows()[5].bytes == written->answer_json[1]);
  CHECK(sink.rows()[0].kind == cms::generate::TITLE_KIND);
  CHECK(sink.rows()[1].kind == cms::generate::STATEMENT_KIND);
}

TEST_CASE("an English-only task writes one statement and passes both checks", "[generate]") {
  auto input = statement_input();
  input.locales = {english()};
  auto client = recorded({answer_json("en")});

  const auto written = cms::generate::write_statement(client, input, oracle_check());
  REQUIRE(written.has_value());
  REQUIRE(written->statements.size() == 1);
  CHECK(written->statements[0].locale == "en");
  CHECK(client.prompts().size() == 1);

  const auto subject = LeakSubject{.locale = "en",
                                   .title = written->statements[0].title,
                                   .statement = cms::generate::render_statement(written->statements[0], written->example)};
  CHECK(check_brief_leak({subject}, StatementRequest{.brief = "A range-sum task.", .description = "Ranges."},
                         plan_texts())
            .has_value());
  CHECK(check_algo_leak({subject}, prefix_sums()).has_value());

  auto sink = MemoryStatementSink{};
  REQUIRE(sink.record(12, *written, oracle_check(), LeakProof{.is_brief_checked = true, .is_algo_checked = true})
              .has_value());
  CHECK(sink.rows().size() == 3);
}

TEST_CASE("a third locale is written by its own call and checked like any other", "[generate]") {
  auto input = statement_input();
  input.locales = {english(), cms::generate::LocaleDeclaration{.tag = "fr", .name = "French"}};
  auto client = recorded({answer_json("en"), answer_json("fr")});

  const auto written = cms::generate::write_statement(client, input, oracle_check());
  REQUIRE(written.has_value());
  REQUIRE(written->statements.size() == 2);
  CHECK(written->statements[1].locale == "fr");
  REQUIRE(client.prompts().size() == 2);
  CHECK(client.prompts()[0].find("English") != std::string::npos);
  CHECK(client.prompts()[0].find("French") == std::string::npos);
  CHECK(client.prompts()[1].find("French") != std::string::npos);
  CHECK(client.prompts()[1].find("English") == std::string::npos);

  const auto french = LeakSubject{.locale = "fr",
                                  .title = written->statements[1].title,
                                  .statement = cms::generate::render_statement(written->statements[1], written->example)};
  CHECK(check_algo_leak({french}, prefix_sums()).has_value());

  auto leaky = french;
  leaky.statement += "\nEach query asks for the sum of the array between two indices chosen by the reader.";
  const auto refused = check_brief_leak(
      {leaky},
      StatementRequest{.brief = "Each query asks for the sum of the array between two indices chosen by the reader.",
                       .description = "Ranges."},
      plan_texts());
  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("fr") != std::string::npos);

  auto sink = MemoryStatementSink{};
  REQUIRE(sink.record(13, *written, oracle_check(), LeakProof{.is_brief_checked = true, .is_algo_checked = true})
              .has_value());
  CHECK(sink.rows().size() == 6);
}

TEST_CASE("each locale is written from its own instruction, never from another locale's text", "[generate]") {
  auto input = statement_input();
  input.locales = {english(), cms::generate::LocaleDeclaration{.tag = "fr", .name = "French"}};
  auto client = recorded({answer_json("en", {}, "MARKER-EN-ONLY"), answer_json("fr", {}, "MARKER-FR-ONLY")});

  const auto written = cms::generate::write_statement(client, input, oracle_check());

  REQUIRE(written.has_value());
  REQUIRE(client.prompts().size() == 2);
  for (const auto& prompt : client.prompts()) {
    CHECK(prompt.find("MARKER-EN-ONLY") == std::string::npos);
    CHECK(prompt.find("MARKER-FR-ONLY") == std::string::npos);
  }
  CHECK(client.prompts()[1].find(written->statements[0].title) == std::string::npos);
  CHECK(client.prompts()[1].find(written->statements[0].sections.task) == std::string::npos);
  CHECK(client.prompts()[1].find("French") != std::string::npos);
  CHECK(client.prompts()[0].find("English") != std::string::npos);
}

TEST_CASE("a declared set without English, with a duplicate, or with a malformed tag is refused by name",
          "[generate]") {
  auto client = recorded({answer_json("en"), answer_json("th")});

  auto no_english = statement_input();
  no_english.locales = {cms::generate::LocaleDeclaration{.tag = "th", .name = "Thai"}};
  const auto without_english = cms::generate::write_statement(client, no_english, oracle_check());
  REQUIRE(!without_english.has_value());
  CHECK(without_english.error().message.find("\"en\"") != std::string::npos);
  CHECK(without_english.error().message.find("English") != std::string::npos);

  auto duplicated = statement_input();
  duplicated.locales = {english(), english()};
  const auto twice = cms::generate::write_statement(client, duplicated, oracle_check());
  REQUIRE(!twice.has_value());
  CHECK(twice.error().message.find("twice") != std::string::npos);

  auto malformed = statement_input();
  malformed.locales = {english(), cms::generate::LocaleDeclaration{.tag = "EN", .name = "English"}};
  const auto bad_tag = cms::generate::write_statement(client, malformed, oracle_check());
  REQUIRE(!bad_tag.has_value());
  CHECK(bad_tag.error().message.find("\"EN\"") != std::string::npos);

  auto nameless = statement_input();
  nameless.locales = {cms::generate::LocaleDeclaration{.tag = "en", .name = ""}};
  const auto no_name = cms::generate::write_statement(client, nameless, oracle_check());
  REQUIRE(!no_name.has_value());
  CHECK(no_name.error().message.find("name") != std::string::npos);
  CHECK(client.prompts().empty());
}

TEST_CASE("the prompt is the specification's own fields, and the request cannot reach it", "[generate]") {
  const auto request = StatementRequest{.brief = "SECRET-REQUEST-BY-THE-TEACHER with extra words",
                                        .description = "SECRET-DESCRIPTION"};
  const auto prompt = cms::generate::statement_prompt(specification(), "the answer looks cheap", english());

  REQUIRE(prompt.has_value());
  CHECK(prompt->find("SECRET-REQUEST") == std::string::npos);
  CHECK(prompt->find("SECRET-DESCRIPTION") == std::string::npos);
  CHECK(prompt->find(specification().input) != std::string::npos);
  CHECK(prompt->find(specification().output) != std::string::npos);
  CHECK(prompt->find("n <= 5") != std::string::npos);
  CHECK(prompt->find("n <= 3") != std::string::npos);
  CHECK(prompt->find("the answer looks cheap") != std::string::npos);
  CHECK(prompt->find("Prefix sums") == std::string::npos);
  CHECK(prompt->find(prefix_sums().thai) == std::string::npos);
  CHECK(request.brief.find("SECRET") != std::string::npos);
}

TEST_CASE("a plan without a trap sentence, or a trap that names the technique, is refused by name", "[generate]") {
  const auto absent = cms::generate::trap_sentence(R"({"approach": "prefix sums"})");
  REQUIRE(!absent.has_value());
  CHECK(absent.error().message.find("trap") != std::string::npos);
  CHECK(absent.error().message.find("refused rather than guessed") != std::string::npos);

  const auto present = cms::generate::trap_sentence(R"({"trap": "the answer looks cheap"})");
  REQUIRE(present.has_value());
  CHECK(*present == "the answer looks cheap");

  auto client = recorded({answer_json("en"), answer_json("th")});
  const auto written =
      cms::generate::write_statement(client, statement_input("a prefix sums answer is fast enough"), oracle_check());

  REQUIRE(!written.has_value());
  CHECK(written.error().message.find("names the technique") != std::string::npos);
  CHECK(written.error().message.find("prefix-sums") != std::string::npos);
  CHECK(client.prompts().empty());
}

TEST_CASE("eight repeated English words are refused, and the message quotes the statement's span", "[generate]") {
  const auto request = StatementRequest{
      .brief = "Each query asks for the sum of the array between two indices chosen by the reader please.",
      .description = "Range sums over an array."};

  const auto clean = check_brief_leak({clean_subject()}, request, plan_texts());
  REQUIRE(clean.has_value());

  auto leaky = clean_subject();
  leaky.statement += "\nThe task is easy: each query asks for the sum of the array between two indices chosen by "
                     "the reader, so add them up.";
  const auto refused = check_brief_leak({leaky}, request, plan_texts());

  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("8 words") != std::string::npos);
  CHECK(refused.error().message.find("the brief") != std::string::npos);
  CHECK(refused.error().message.find("each query asks for the sum of the") != std::string::npos);
  CHECK(refused.error().message.find("please") == std::string::npos);

  auto shouty = clean_subject();
  shouty.statement += "\nEACH QUERY ASKS FOR THE SUM OF THE ARRAY BETWEEN TWO INDICES CHOSEN BY THE READER.";
  const auto case_refused = check_brief_leak({shouty}, request, plan_texts());
  REQUIRE(!case_refused.has_value());
  CHECK(case_refused.error().message.find("8 words") != std::string::npos);
}

TEST_CASE("a Thai span broken across a line is still one copied span", "[generate]") {
  const auto request = StatementRequest{.brief = std::string{THAI_BRIEF}, .description = "ผลรวมช่วงข้อมูล"};
  const auto head = first_characters(THAI_BRIEF, 16);
  const auto forty = first_characters(THAI_BRIEF, 40);

  auto leaky = clean_subject("th");
  leaky.statement = head + "\n" + forty.substr(head.size());
  const auto refused = check_brief_leak({leaky}, request, plan_texts());

  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("32 characters") != std::string::npos);
}

TEST_CASE("thirty-two repeated Thai characters are refused, thirty-one are not", "[generate]") {
  const auto request = StatementRequest{.brief = std::string{THAI_BRIEF}, .description = "ผลรวมช่วงข้อมูล"};

  auto leaky = clean_subject("th");
  leaky.statement = first_characters(THAI_BRIEF, 32) + "\n\n tugas นี้";
  const auto refused = check_brief_leak({leaky}, request, plan_texts());
  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("32 characters") != std::string::npos);
  CHECK(refused.error().message.find("the brief") != std::string::npos);

  auto almost = clean_subject("th");
  almost.statement = first_characters(THAI_BRIEF, 31) + "x";
  const auto clean = check_brief_leak({almost}, request, plan_texts());
  REQUIRE(clean.has_value());
}

TEST_CASE("the plan's reason is one of the four texts the check compares", "[generate]") {
  auto plan = plan_texts();
  plan.reason = "the plan is worth building because it exercises the array between two indices chosen by the reader";
  const auto request = StatementRequest{.brief = "An array task.", .description = "Ranges."};

  auto leaky = clean_subject();
  leaky.statement += "\nIt exercises the array between two indices chosen by the reader, which is the point.";
  const auto refused = check_brief_leak({leaky}, request, plan);

  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("the plan's reason") != std::string::npos);
  CHECK(refused.error().message.find("8 words") != std::string::npos);
}

TEST_CASE("the algorithm check matches the id, both labels, the acronym, and passes the clean title", "[generate]") {
  const auto dijkstra = TechniqueNames{
      .id = "dijkstra", .english = "Shortest paths", .thai = "เส้นทางสั้นที่สุด", .acronym = "sp"};
  const auto by_id = check_algo_leak(
      {LeakSubject{.locale = "en", .title = "Using Dijkstra", .statement = "plain"}}, dijkstra);
  REQUIRE(!by_id.has_value());
  CHECK(by_id.error().message.find("dijkstra") != std::string::npos);

  const auto by_label = check_algo_leak(
      {LeakSubject{.locale = "en", .title = "A grid", .statement = "Compute the shortest paths first."}},
      dijkstra);
  REQUIRE(!by_label.has_value());
  CHECK(by_label.error().message.find("Shortest paths") != std::string::npos);

  const auto by_thai = check_algo_leak(
      {LeakSubject{.locale = "th", .title = "ตาราง", .statement = "ใช้เส้นทางสั้นที่สุด"}}, dijkstra);
  REQUIRE(!by_thai.has_value());
  CHECK(by_thai.error().message.find("เส้นทางสั้นที่สุด") != std::string::npos);

  const auto by_acronym = check_algo_leak(
      {LeakSubject{.locale = "en", .title = "A grid", .statement = "build an sp table"}}, dijkstra);
  REQUIRE(!by_acronym.has_value());

  auto derived = dijkstra;
  derived.acronym.clear();
  const auto by_derived_acronym = check_algo_leak(
      {LeakSubject{.locale = "en", .title = "A grid", .statement = "build an sp table"}}, derived);
  REQUIRE(!by_derived_acronym.has_value());
  CHECK(cms::generate::technique_acronym("Minimum spanning tree") == "mst");
  CHECK(cms::generate::technique_acronym("Greedy").empty());

  const auto bfs = TechniqueNames{
      .id = "bfs", .english = "Breadth-first search", .thai = "การค้นหาแบบกว้าง", .acronym = "bfs"};
  const auto sample = check_algo_leak(
      {LeakSubject{.locale = "en",
                   .title = "Shortest Path in a Grid",
                   .statement = "You are given a grid and must reach the bottom-right cell."}},
      bfs);
  REQUIRE(sample.has_value());
}

TEST_CASE("the technique's four names come from the vocabulary, and an id it does not define is refused",
          "[generate]") {
  auto workspace = LeakWorkspace::create();
  REQUIRE(workspace.has_value());
  const auto vocabulary = workspace->directory() / "topics.json";
  {
    auto file = std::ofstream{vocabulary};
    file << R"({"topics":[{"id":"dijkstra","en":"Shortest paths","th":"\u0e40\u0e2a\u0e49\u0e19\u0e17\u0e32\u0e07\u0e2a\u0e31\u0e49\u0e19\u0e17\u0e35\u0e48\u0e2a\u0e38\u0e14"}]})";
  }

  const auto names = cms::generate::read_technique_names(vocabulary, "dijkstra");
  REQUIRE(names.has_value());
  CHECK(names->english == "Shortest paths");
  CHECK(names->thai == "เส้นทางสั้นที่สุด");
  CHECK(names->acronym == "sp");

  const auto unknown = cms::generate::read_technique_names(vocabulary, "nope");
  REQUIRE(!unknown.has_value());
  CHECK(unknown.error().message.find("nope") != std::string::npos);
}

TEST_CASE("a job row with an empty request is refused by name, and nothing is compared", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "statement-empty-request-probe");

  const auto empty = cms::generate::read_statement_request(*cms::generate::test::live_client(), job);
  REQUIRE(!empty.has_value());
  CHECK(empty.error().message.find("stores no request") != std::string::npos);
  CHECK(empty.error().message.find("brief and description are empty") != std::string::npos);

  const auto compared = check_brief_leak({clean_subject()}, StatementRequest{}, PlanTexts{});
  REQUIRE(!compared.has_value());
  CHECK(compared.error().message.find("stores no request") != std::string::npos);

  store_request(database, job, "SECRET-REQUEST-BY-THE-TEACHER", "Range sums");
  const auto stored = cms::generate::read_statement_request(*cms::generate::test::live_client(), job);
  REQUIRE(stored.has_value());
  CHECK(stored->brief == "SECRET-REQUEST-BY-THE-TEACHER");

  const auto prompt = cms::generate::statement_prompt(specification(), "the answer looks cheap", english());
  REQUIRE(prompt.has_value());
  CHECK(prompt->find(stored->brief) == std::string::npos);
}

TEST_CASE("a leaked statement writes no row and ends the claim failed", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  database.execute_text("DELETE FROM generation_jobs", {});
  const auto job = cms::generate::test::prepare_job(database, "statement-failed-claim-probe");
  store_request(database, job,
                "Each query asks for the sum of the array between two indices chosen by the reader.", "Range sums");

  const auto claim = cms::generate::claim_job(database, "statement-worker");
  REQUIRE(claim.has_value());
  REQUIRE(claim->has_value());
  const auto claimed = **claim;
  CHECK(claimed.id == job);

  const auto leaky_task =
      std::string{"Each query asks for the sum of the array between two indices chosen by the reader, and the "
                  "answer is printed"};
  auto client = recorded({answer_json("en", {}, leaky_task), answer_json("th")});
  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());
  REQUIRE(written.has_value());

  const auto request = cms::generate::read_statement_request(*cms::generate::test::live_client(), claimed.id);
  REQUIRE(request.has_value());
  const auto refused = check_brief_leak(
      {LeakSubject{.locale = "en",
                   .title = written->statements[0].title,
                   .statement = cms::generate::render_statement(written->statements[0], written->example)}},
      *request, plan_texts());
  REQUIRE(!refused.has_value());

  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlStatementSink{*cms::generate::test::live_client(), store};
  const auto refused_write = sink.record(claimed.id, *written, oracle_check(), LeakProof{});
  REQUIRE(!refused_write.has_value());
  CHECK(refused_write.error().message.find("check:brief-leak") != std::string::npos);
  CHECK(artefact_rows(database, claimed.id) == 0);

  CHECK(cms::generate::finish_claim(database, claimed.id, "statement-worker", "failed").has_value());
  CHECK(job_state(database, claimed.id) == "failed");
}

TEST_CASE("both locale rows are written, and read back one locale at a time", "[generate][live]") {
  if (!cms::generate::test::has_live_database()) {
    SKIP("CMSLEAN_TEST_DSN is not set");
  }
  auto database = cms::generate::test::live_database();
  const auto job = cms::generate::test::prepare_job(database, "statement-locale-probe");
  auto client = recorded({answer_json("en"), answer_json("th")});
  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());
  REQUIRE(written.has_value());

  auto store = cms::generate::test::MemoryStore{};
  auto sink = cms::generate::SqlStatementSink{*cms::generate::test::live_client(), store};
  const auto rows = sink.record(job, *written, oracle_check(),
                                LeakProof{.is_brief_checked = true, .is_algo_checked = true});
  REQUIRE(rows.has_value());
  CHECK(*rows == 6);

  const auto english = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job,
                                                     cms::generate::STATEMENT_STEP, cms::generate::STATEMENT_KIND,
                                                     cms::generate::ENGLISH_LOCALE);
  const auto thai = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job,
                                                  cms::generate::STATEMENT_STEP, cms::generate::STATEMENT_KIND,
                                                  "th");
  REQUIRE(english.has_value());
  REQUIRE(thai.has_value());
  CHECK(english->bytes.find("## Task") != std::string::npos);
  CHECK(thai->bytes != english->bytes);

  const auto missing = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job,
                                                     cms::generate::STATEMENT_STEP, cms::generate::STATEMENT_KIND,
                                                     "fr");
  REQUIRE(!missing.has_value());
  CHECK(missing.error().code == cms::ErrorCode::not_found);
  CHECK(missing.error().message.find("fr") != std::string::npos);

  const auto newest = cms::generate::read_artefact(*cms::generate::test::live_client(), store, job,
                                                    cms::generate::STATEMENT_STEP, cms::generate::STATEMENT_KIND);
  REQUIRE(newest.has_value());
  CHECK(newest->bytes == thai->bytes);
}

TEST_CASE("two runs over the same recorded answers produce identical bytes", "[generate]") {
  auto first = recorded({answer_json("en"), answer_json("th")});
  auto second = recorded({answer_json("en"), answer_json("th")});

  const auto one = cms::generate::write_statement(first, statement_input(), oracle_check());
  const auto two = cms::generate::write_statement(second, statement_input(), oracle_check());

  REQUIRE(one.has_value());
  REQUIRE(two.has_value());
  for (std::size_t index = 0; index < one->statements.size(); ++index) {
    CHECK(one->statements[index].title == two->statements[index].title);
    CHECK(cms::generate::render_statement(one->statements[index], one->example) ==
          cms::generate::render_statement(two->statements[index], two->example));
  }
}

TEST_CASE("the check's temporary copy is removed, and the request never reaches a row or a refusal",
          "[generate]") {
  auto workspace = LeakWorkspace::create();
  REQUIRE(workspace.has_value());
  const auto directory = workspace->directory().string();
  CHECK(std::filesystem::exists(directory));
  CHECK(directory.find("/content/") == std::string::npos);

  const auto request = StatementRequest{.brief = "SECRET-REQUEST brief words here",
                                        .description = "SECRET-DESCRIPTION one line"};
  REQUIRE(workspace->write(request).has_value());
  CHECK(std::filesystem::exists(workspace->path(cms::generate::leak_file::BRIEF)));
  CHECK(std::filesystem::exists(workspace->path(cms::generate::leak_file::DESCRIPTION)));
  REQUIRE(workspace->write(plan_texts()).has_value());
  CHECK(std::filesystem::exists(workspace->path(cms::generate::leak_file::PLAN_REASON)));
  REQUIRE(workspace->write(prefix_sums()).has_value());
  CHECK(std::filesystem::exists(workspace->path(cms::generate::leak_file::TECHNIQUE)));

  auto client = recorded({answer_json("en"), answer_json("th")});
  const auto written = cms::generate::write_statement(client, statement_input(), oracle_check());
  REQUIRE(written.has_value());
  REQUIRE(workspace->write(*written).has_value());
  CHECK(std::filesystem::exists(workspace->path(cms::generate::title_file("en"))));
  CHECK(std::filesystem::exists(workspace->path(cms::generate::statement_file("th"))));
  CHECK(std::filesystem::exists(workspace->path(cms::generate::leak_file::LOCALES)));

  auto sink = MemoryStatementSink{};
  REQUIRE(sink.record(3, *written, oracle_check(),
                      LeakProof{.is_brief_checked = true, .is_algo_checked = true})
              .has_value());
  for (const auto& row : sink.rows()) {
    CHECK(row.bytes.find("SECRET-REQUEST") == std::string::npos);
    CHECK(row.bytes.find("SECRET-DESCRIPTION") == std::string::npos);
  }

  auto leaky = clean_subject();
  leaky.statement += "\nEach query asks for the sum of the array between two indices chosen by the reader.";
  const auto refused = check_brief_leak(
      {leaky},
      StatementRequest{.brief = "Each query asks for the sum of the array between two indices chosen by the reader. "
                                "SECRET-REQUEST",
                       .description = "SECRET-DESCRIPTION"},
      plan_texts());
  REQUIRE(!refused.has_value());
  CHECK(refused.error().message.find("SECRET-REQUEST") == std::string::npos);
  CHECK(refused.error().message.find("SECRET-DESCRIPTION") == std::string::npos);

  workspace->remove();
  CHECK(!std::filesystem::exists(directory));
}
