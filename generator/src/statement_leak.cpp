#include <cms/generate/statement.hpp>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "text.hpp"

namespace cms::generate {
namespace {

using detail::trimmed;

constexpr std::size_t ENGLISH_WORDS = 8;
constexpr std::size_t THAI_CHARACTERS = 32;

struct Source {
  std::string_view name;
  std::string text;
};

auto decoded(std::string_view text) -> std::vector<std::uint32_t> {
  auto points = std::vector<std::uint32_t>{};
  points.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    const auto byte = static_cast<unsigned char>(text[index]);
    auto width = std::size_t{1};
    auto value = static_cast<std::uint32_t>(byte);
    if (byte >= 0xF0) {
      width = 4;
      value = byte & 0x07U;
    } else if (byte >= 0xE0) {
      width = 3;
      value = byte & 0x0FU;
    } else if (byte >= 0xC0) {
      width = 2;
      value = byte & 0x1FU;
    }
    if (index + width > text.size()) {
      break;
    }
    for (std::size_t offset = 1; offset < width; ++offset) {
      value = (value << 6U) | (static_cast<unsigned char>(text[index + offset]) & 0x3FU);
    }
    points.push_back(value);
    index += width;
  }
  return points;
}

auto append_utf8(std::string& bytes, std::uint32_t point) -> void {
  if (point < 0x80U) {
    bytes.push_back(static_cast<char>(point));
  } else if (point < 0x800U) {
    bytes.push_back(static_cast<char>(0xC0U | (point >> 6U)));
    bytes.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
  } else if (point < 0x10000U) {
    bytes.push_back(static_cast<char>(0xE0U | (point >> 12U)));
    bytes.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
    bytes.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
  } else {
    bytes.push_back(static_cast<char>(0xF0U | (point >> 18U)));
    bytes.push_back(static_cast<char>(0x80U | ((point >> 12U) & 0x3FU)));
    bytes.push_back(static_cast<char>(0x80U | ((point >> 6U) & 0x3FU)));
    bytes.push_back(static_cast<char>(0x80U | (point & 0x3FU)));
  }
}

auto encoded(const std::vector<std::uint32_t>& points, std::size_t first, std::size_t count) -> std::string {
  auto bytes = std::string{};
  for (std::size_t index = first; index < first + count && index < points.size(); ++index) {
    append_utf8(bytes, points[index]);
  }
  return bytes;
}

auto is_thai(std::uint32_t point) -> bool {
  return point >= 0x0E00U && point <= 0x0E7FU;
}

/// The combining class of the Thai marks that carry one. A mark the table does not name is a starter, so the
/// canonical order leaves it where the writer put it.
auto combining_class(std::uint32_t point) -> int {
  switch (point) {
    case 0x0E38U:
    case 0x0E39U:
      return 103;
    case 0x0E3AU:
      return 9;
    case 0x0E48U:
    case 0x0E49U:
    case 0x0E4AU:
    case 0x0E4BU:
      return 107;
    case 0x0E4CU:
      return 108;
    default:
      return 0;
  }
}

auto emphasised(std::uint32_t point) -> bool {
  return point == '*' || point == '_' || point == '`' || point == '#' || point == '>';
}

/// @return @p text with its emphasis markers gone, its whitespace collapsed, ASCII lowercased and its Thai
/// combining marks in canonical order, so two spellings of one sentence compare equal (ADR 0037 §4).
auto folded(std::string_view text) -> std::string {
  auto points = decoded(text);
  for (std::size_t index = 0; index < points.size();) {
    const auto point = points[index];
    if (combining_class(point) == 0 || index == 0) {
      ++index;
      continue;
    }
    auto end = index;
    while (end < points.size() && combining_class(points[end]) > 0) {
      ++end;
    }
    for (auto left = index; left < end; ++left) {
      for (auto right = left + 1; right < end; ++right) {
        if (combining_class(points[right]) < combining_class(points[left])) {
          std::swap(points[left], points[right]);
        }
      }
    }
    index = end;
  }
  auto bytes = std::string{};
  bytes.reserve(points.size());
  auto spaced = false;
  for (const auto point : points) {
    if (emphasised(point) || point == ' ' || point == '\t' || point == '\n' || point == '\r') {
      spaced = !bytes.empty();
      continue;
    }
    if (spaced) {
      bytes.push_back(' ');
      spaced = false;
    }
    if (point >= 'A' && point <= 'Z') {
      bytes.push_back(static_cast<char>(point - 'A' + 'a'));
      continue;
    }
    append_utf8(bytes, point);
  }
  return bytes;
}

auto words_of(std::string_view text) -> std::vector<std::string> {
  auto words = std::vector<std::string>{};
  auto current = std::string{};
  for (const auto character : text) {
    if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
      current.push_back(character);
      continue;
    }
    if (!current.empty()) {
      words.push_back(std::move(current));
      current.clear();
    }
  }
  if (!current.empty()) {
    words.push_back(std::move(current));
  }
  return words;
}

auto joined(const std::vector<std::string>& words, std::size_t first, std::size_t count) -> std::string {
  auto text = std::string{};
  for (std::size_t index = first; index < first + count && index < words.size(); ++index) {
    text += index == first ? "" : " ";
    text += words[index];
  }
  return text;
}

auto repeated_words(std::string_view subject, std::string_view source) -> std::optional<std::string> {
  const auto source_words = words_of(source);
  if (source_words.size() < ENGLISH_WORDS) {
    return std::nullopt;
  }
  const auto subject_words = words_of(subject);
  if (subject_words.size() < ENGLISH_WORDS) {
    return std::nullopt;
  }
  auto spans = std::unordered_set<std::string>{};
  for (std::size_t index = 0; index + ENGLISH_WORDS <= source_words.size(); ++index) {
    spans.insert(joined(source_words, index, ENGLISH_WORDS));
  }
  for (std::size_t index = 0; index + ENGLISH_WORDS <= subject_words.size(); ++index) {
    auto span = joined(subject_words, index, ENGLISH_WORDS);
    if (spans.contains(span)) {
      return span;
    }
  }
  return std::nullopt;
}

/// @return @p text without its spaces, so a line break inside a copied Thai span cannot hide it from the
/// character rule (ADR 0037 §4).
auto compacted(std::string_view text) -> std::string {
  auto bytes = std::string{};
  bytes.reserve(text.size());
  for (const auto character : text) {
    if (character != ' ') {
      bytes.push_back(character);
    }
  }
  return bytes;
}

auto repeated_characters(std::string_view subject, std::string_view source) -> std::optional<std::string> {
  const auto points = decoded(subject);
  if (points.size() < THAI_CHARACTERS) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index + THAI_CHARACTERS <= points.size(); ++index) {
    auto has_thai = false;
    for (std::size_t offset = 0; offset < THAI_CHARACTERS; ++offset) {
      if (is_thai(points[index + offset])) {
        has_thai = true;
        break;
      }
    }
    if (!has_thai) {
      continue;
    }
    auto span = encoded(points, index, THAI_CHARACTERS);
    if (source.find(span) != std::string_view::npos) {
      return span;
    }
  }
  return std::nullopt;
}

auto acronym_of(std::string_view english) -> std::string {
  auto letters = std::string{};
  auto is_word_start = true;
  for (const auto character : english) {
    if (std::isalpha(static_cast<unsigned char>(character)) == 0) {
      is_word_start = true;
      continue;
    }
    if (is_word_start) {
      letters.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    is_word_start = false;
  }
  return letters.size() >= 2 ? letters : std::string{};
}

auto named_in(std::string_view text, std::string_view name) -> bool {
  if (name.empty()) {
    return false;
  }
  const auto name_points = decoded(name);
  for (const auto point : name_points) {
    if (is_thai(point)) {
      return text.find(folded(name)) != std::string_view::npos;
    }
  }
  const auto name_words = words_of(folded(name));
  if (name_words.empty()) {
    return false;
  }
  const auto text_words = words_of(text);
  if (name_words.size() > text_words.size()) {
    return false;
  }
  for (std::size_t index = 0; index + name_words.size() <= text_words.size(); ++index) {
    auto matches = true;
    for (std::size_t offset = 0; offset < name_words.size(); ++offset) {
      if (text_words[index + offset] != name_words[offset]) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return true;
    }
  }
  return false;
}

auto surface_of(const LeakSubject& subject) -> std::array<std::pair<std::string_view, std::string>, 2> {
  return {{{"title", subject.title}, {"statement", subject.statement}}};
}

auto refusal(const LeakSubject& subject, std::string_view surface, std::string_view unit,
             std::string_view source, std::string_view span) -> Error {
  return Error::make(ErrorCode::invalid_argument, "the " + std::string{surface} + " (" + subject.locale +
                                                      ") repeats " + std::string{unit} + " of " +
                                                      std::string{source} + ": \"" + std::string{span} + "\"");
}

auto read_text(const char* path) -> Result<std::string> {
  auto stream = std::ifstream{std::string{path}, std::ios::binary};
  if (!stream) {
    return std::unexpected(
        Error::make(ErrorCode::not_found, "the leak check cannot read " + std::string{path}));
  }
  auto buffer = std::ostringstream{};
  buffer << stream.rdbuf();
  return buffer.str();
}

auto run_brief(int argc, char** argv) -> int {
  if (argc != 12) {
    std::fprintf(stderr,
                 "usage: cms_generate_leak brief <brief> <description> <plan-approach> <plan-complexity> "
                 "<plan-body> <plan-reason> <title-en> <title-th> <statement-en> <statement-th>\n");
    return 64;
  }
  auto texts = std::array<std::string, 10>{};
  for (std::size_t index = 0; index < texts.size(); ++index) {
    auto text = read_text(argv[static_cast<int>(index) + 2]);
    if (!text) {
      std::fprintf(stderr, "%s\n", text.error().message.c_str());
      return 2;
    }
    texts[index] = std::move(*text);
  }
  const auto request = StatementRequest{.brief = texts[0], .description = texts[1]};
  const auto plan = PlanTexts{
      .approach = texts[2], .intended_complexity = texts[3], .plan = texts[4], .reason = texts[5]};
  const auto locales = std::array<std::pair<std::string_view, std::size_t>, 2>{{{"en", 6}, {"th", 7}}};
  for (const auto& [locale, first] : locales) {
    const auto subject =
        LeakSubject{.locale = std::string{locale}, .title = texts[first], .statement = texts[first + 2]};
    const auto checked = check_brief_leak(subject, request, plan);
    if (!checked) {
      std::fprintf(stderr, "%s\n", checked.error().message.c_str());
      return 1;
    }
  }
  return 0;
}

auto run_algo(int argc, char** argv) -> int {
  if (argc != 10) {
    std::fprintf(stderr,
                 "usage: cms_generate_leak algo <technique-id> <technique-en> <technique-th> <acronym> "
                 "<title-en> <title-th> <statement-en> <statement-th>\n");
    return 64;
  }
  const auto technique = TechniqueNames{
      .id = argv[2], .english = argv[3], .thai = argv[4], .acronym = std::string{argv[5]} == "-" ? "" : argv[5]};
  auto texts = std::array<std::string, 4>{};
  for (std::size_t index = 0; index < texts.size(); ++index) {
    auto text = read_text(argv[static_cast<int>(index) + 6]);
    if (!text) {
      std::fprintf(stderr, "%s\n", text.error().message.c_str());
      return 2;
    }
    texts[index] = std::move(*text);
  }
  const auto locales = std::array<std::pair<std::string_view, std::size_t>, 2>{{{"en", 0}, {"th", 1}}};
  for (const auto& [locale, first] : locales) {
    const auto subject =
        LeakSubject{.locale = std::string{locale}, .title = texts[first], .statement = texts[first + 2]};
    const auto checked = check_algo_leak(subject, technique);
    if (!checked) {
      std::fprintf(stderr, "%s\n", checked.error().message.c_str());
      return 1;
    }
  }
  return 0;
}

}  // namespace

auto technique_acronym(std::string_view english) -> std::string {
  return acronym_of(english);
}

auto named_technique(std::string_view text, const TechniqueNames& technique) -> std::optional<std::string> {
  const auto subject = folded(text);
  const auto acronym = technique.acronym.empty() ? technique_acronym(technique.english) : technique.acronym;
  for (const auto& name : {technique.id, technique.english, technique.thai, acronym}) {
    if (named_in(subject, name)) {
      return name;
    }
  }
  return std::nullopt;
}

auto check_brief_leak(const LeakSubject& subject, const StatementRequest& request, const PlanTexts& plan)
    -> Result<void> {
  if (trimmed(request.brief).empty() && trimmed(request.description).empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument,
        "the job row stores no request: brief and description are empty, so there is nothing to compare the " +
            subject.locale + " title and statement against"));
  }
  if (trimmed(request.brief).empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "the job row stores no brief; ADR 0036 §2 writes it when the job "
                                                 "is created, and a check against nothing is not a check"));
  }
  if (trimmed(request.description).empty()) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, "the job row stores no description; ADR 0036 §2 writes it when the job is "
                                     "created, and a check against nothing is not a check"));
  }
  const auto sources = std::array<Source, 6>{{{"the brief", folded(request.brief)},
                                              {"the description", folded(request.description)},
                                              {"the plan's approach", folded(plan.approach)},
                                              {"the plan's intended complexity", folded(plan.intended_complexity)},
                                              {"the plan", folded(plan.plan)},
                                              {"the plan's reason", folded(plan.reason)}}};
  for (auto& [surface, text] : surface_of(subject)) {
    const auto folded_text = folded(text);
    for (const auto& source : sources) {
      if (source.text.empty()) {
        continue;
      }
      if (const auto span = repeated_words(folded_text, source.text)) {
        return std::unexpected(refusal(subject, surface, "8 words", source.name, *span));
      }
      if (const auto span = repeated_characters(compacted(folded_text), compacted(source.text))) {
        return std::unexpected(refusal(subject, surface, "32 characters", source.name, *span));
      }
    }
  }
  return {};
}

auto check_algo_leak(const LeakSubject& subject, const TechniqueNames& technique) -> Result<void> {
  if (technique.id.empty() && technique.english.empty() && technique.thai.empty() && technique.acronym.empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "the algorithm check was handed no technique name to match; the plan names "
                                       "the technique and the vocabulary names its labels"));
  }
  for (const auto& [surface, text] : surface_of(subject)) {
    const auto name = named_technique(text, technique);
    if (name) {
      return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                         "the " + std::string{surface} + " (" + subject.locale +
                                             ") names the intended approach \"" + *name +
                                             "\"; a statement never names the technique (ADR 0036 §3)"));
    }
  }
  return {};
}

auto leak_command(int argc, char** argv) -> int {
  if (argc < 2) {
    std::fprintf(stderr, "usage: cms_generate_leak <brief|algo> <files...>\n");
    return 64;
  }
  const auto mode = std::string_view{argv[1]};
  if (mode == "brief") {
    return run_brief(argc, argv);
  }
  if (mode == "algo") {
    return run_algo(argc, argv);
  }
  std::fprintf(stderr, "usage: cms_generate_leak <brief|algo> <files...>\n");
  return 64;
}

}  // namespace cms::generate
