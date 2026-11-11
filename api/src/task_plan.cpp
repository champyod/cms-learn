#include <cms/api/task_plan.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <exception>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/api/audit.hpp>
#include <cms/api/authorize.hpp>
#include <cms/api/form_body.hpp>
#include <cms/capability.hpp>
#include <cms/db/tables.hpp>

namespace cms::api {
namespace {

constexpr std::string_view STATUS_DRAFT = "draft";
constexpr std::string_view VERIFICATION_PENDING = "pending";
constexpr std::string_view CHECKER_TOKEN = "token";
constexpr std::string_view ORIGIN_ORIGINAL = "original";
constexpr std::int64_t DEFAULT_TIME_LIMIT_MS = 1000;
constexpr std::int64_t DEFAULT_MEMORY_LIMIT_MB = 256;

auto to_int(std::string_view text, std::int64_t& number) -> bool {
  const auto parsed = std::from_chars(text.begin(), text.end(), number);
  return parsed.ec == std::errc{} && parsed.ptr == text.end();
}

auto json_string_list(std::string_view text) -> std::vector<std::string> {
  auto values = std::vector<std::string>{};
  auto inside = false;
  auto current = std::string{};
  for (const char character : text) {
    if (character == '"') {
      if (inside) {
        values.push_back(current);
        current.clear();
      }
      inside = !inside;
      continue;
    }
    if (inside) {
      current.push_back(character);
    }
  }
  return values;
}

auto slug_of(std::string_view title) -> std::string {
  auto slug = std::string{};
  for (const char character : title) {
    const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    if ((lowered >= 'a' && lowered <= 'z') || (lowered >= '0' && lowered <= '9')) {
      slug.push_back(lowered);
    } else if (!slug.empty() && slug.back() != '-') {
      slug.push_back('-');
    }
  }
  while (!slug.empty() && slug.back() == '-') {
    slug.pop_back();
  }
  if (slug.empty()) {
    slug = "draft";
  }
  slug.resize(std::min<std::size_t>(slug.size(), 40));
  std::random_device entropy;
  const auto suffix = static_cast<unsigned>(entropy()) % 0xFFFFFFU;
  auto hex = std::array<char, 7>{};
  std::snprintf(hex.data(), hex.size(), "%06x", suffix);
  return slug + "-" + std::string{hex.data()};
}

auto read_type_fields(db::connection& database, const std::string& type_id)
    -> Result<std::vector<FieldDefinitionRow>> {
  auto rows = std::vector<FieldDefinitionRow>{};
  try {
    for (const auto& row : database(sqlpp::select(db::taskFieldDefs.key, db::taskFieldDefs.kind,
                                                  db::taskFieldDefs.labelEn, db::taskFieldDefs.labelTh,
                                                  db::taskFieldDefs.required,
                                                  db::taskFieldDefs.choices,
                                                  db::taskFieldDefs.minimum, db::taskFieldDefs.maximum,
                                                  db::taskFieldDefs.position)
                                        .from(db::taskFieldDefs)
                                        .where(db::taskFieldDefs.taskTypeId ==
                                               std::string{type_id})
                                        .order_by(db::taskFieldDefs.position.asc()))) {
      rows.push_back(FieldDefinitionRow{
          .key = std::string{row.key},
          .kind = std::string{row.kind},
          .label_en = std::string{row.labelEn},
          .label_th = std::string{row.labelTh},
          .is_required = row.required,
          .choices = row.choices.has_value() ? json_string_list(*row.choices) : std::vector<std::string>{},
          .minimum = row.minimum.has_value() ? std::optional<std::int32_t>{
                                                   static_cast<std::int32_t>(*row.minimum)}
                                             : std::nullopt,
          .maximum = row.maximum.has_value() ? std::optional<std::int32_t>{
                                                   static_cast<std::int32_t>(*row.maximum)}
                                             : std::nullopt,
          .position = static_cast<std::int32_t>(row.position)});
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return rows;
}

/// The form's keys and values, decoded the way the browser wrote them. One parser, so the set of keys the
/// request can name is decided here and nowhere else.
auto read_form_fields(std::string_view body) -> std::map<std::string, std::string> {
  auto fields = std::map<std::string, std::string>{};
  auto rest = body;
  while (!rest.empty()) {
    const auto separator = rest.find('&');
    const auto pair = rest.substr(0, separator);
    rest.remove_prefix(separator == std::string_view::npos ? rest.size() : separator + 1);
    if (pair.empty()) {
      continue;
    }
    const auto equals = pair.find('=');
    const auto name = pair.substr(0, equals);
    const auto value = equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1);
    fields[std::string{name}] = std::string{form_field("k=" + std::string{value}, "k")};
  }
  return fields;
}

/// Every key the form owns is allowed; every key a URL or a hostile field name could add is not. The refusal
/// names the key, because an author who sent `status=published` needs to read which key was refused.
auto is_form_key(std::string_view key) -> bool {
  // The token is the form's too: the route compared it against the session before this action ran, and a body
  // that carries it is every real submission rather than an attempt to set something.
  return key == TYPE_KEY || key == TITLE_EN_KEY || key == TITLE_TH_KEY || key == DIFFICULTY_KEY ||
         key == "csrf_token";
}

}  // namespace

auto read_task_types(db::connection& database) -> Result<std::vector<TaskTypeRow>> {
  auto types = std::vector<TaskTypeRow>{};
  try {
    for (const auto& row : database(sqlpp::select(db::taskTypes.id, db::taskTypes.titleEn,
                                                  db::taskTypes.titleTh)
                                        .from(db::taskTypes)
                                        .order_by(db::taskTypes.position.asc()))) {
      auto fields = read_type_fields(database, std::string{row.id});
      if (!fields) {
        return std::unexpected(fields.error());
      }
      types.push_back(TaskTypeRow{.id = std::string{row.id},
                                  .title_en = std::string{row.titleEn},
                                  .title_th = std::string{row.titleTh},
                                  .fields = std::move(*fields)});
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return types;
}

auto save_task_draft(db::connection& database, const Actor& actor, std::string_view body)
    -> Result<std::string> {
  const auto allowed = authorize(database, actor.kind, actor.id, Capability::content_write);
  if (!allowed) {
    return std::unexpected(allowed.error());
  }

  const auto type_id = std::string{form_field(body, TYPE_KEY)};
  if (type_id.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "the form did not name a task type"));
  }
  const auto fields = read_type_fields(database, type_id);
  if (!fields) {
    return std::unexpected(fields.error());
  }
  if (fields->empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "no task type called " + type_id + " is defined"));
  }

  // The form's own keys, plus exactly the fields this type declares. Anything else is refused by name
  // before a single statement runs, so no request can reach a column the form does not own.
  const auto submitted = read_form_fields(body);
  for (const auto& [key, value] : submitted) {
    if (is_form_key(key)) {
      continue;
    }
    const auto known = std::ranges::any_of(*fields, [&key](const FieldDefinitionRow& field) {
      return field.key == key;
    });
    if (!known) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "the form does not carry the field " + std::string{key} + "; it is not defined for " + type_id));
    }
  }

  auto values = std::map<std::string, std::string>{};
  for (const auto& field : *fields) {
    const auto found = submitted.find(field.key);
    const auto value = found == submitted.end() ? std::string{} : found->second;
    if (value.empty()) {
      if (field.is_required) {
        return std::unexpected(
            Error::make(ErrorCode::invalid_argument, "the field " + field.key + " is required"));
      }
      continue;
    }
    if (field.kind == "choice" &&
        std::ranges::find(field.choices, value) == field.choices.end()) {
      return std::unexpected(Error::make(
          ErrorCode::invalid_argument,
          "the field " + field.key + " accepts one of its choices, not \"" + value + "\""));
    }
    if (field.kind == "integer") {
      auto number = std::int64_t{0};
      if (!to_int(value, number)) {
        return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                           "the field " + field.key + " must be a whole number"));
      }
      if ((field.minimum.has_value() && number < *field.minimum) ||
          (field.maximum.has_value() && number > *field.maximum)) {
        return std::unexpected(Error::make(
            ErrorCode::invalid_argument, "the field " + field.key + " is outside the bounds its type allows"));
      }
    }
    values[field.key] = value;
  }

  const auto title_en = std::string{form_field(body, TITLE_EN_KEY)};
  const auto title_th = std::string{form_field(body, TITLE_TH_KEY)};
  if (title_en.empty() || title_th.empty()) {
    return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                       "a draft needs both titles; the missing one is a required column"));
  }
  auto difficulty = std::int64_t{0};
  if (!to_int(std::string{form_field(body, DIFFICULTY_KEY)}, difficulty) || difficulty < 800 ||
      difficulty > 3500 || difficulty % 100 != 0) {
    return std::unexpected(Error::make(
        ErrorCode::invalid_argument, "the difficulty must be a band edge between 800 and 3500"));
  }

  auto id = slug_of(title_en);
  try {
    database(sqlpp::insert_into(db::tasks).set(
        db::tasks.id = id, db::tasks.version = std::int64_t{1},
        db::tasks.taskType = std::optional<std::string>{type_id},
        db::tasks.status = std::string{STATUS_DRAFT}, db::tasks.titleEn = title_en,
        db::tasks.titleTh = title_th, db::tasks.difficulty = difficulty,
        db::tasks.timeLimitMs = DEFAULT_TIME_LIMIT_MS,
        db::tasks.memoryLimitMb = DEFAULT_MEMORY_LIMIT_MB,
        db::tasks.checker = std::string{CHECKER_TOKEN},
        db::tasks.origin = std::string{ORIGIN_ORIGINAL},
        db::tasks.verification = std::string{VERIFICATION_PENDING},
        db::tasks.solution = std::string{}, db::tasks.contentHash = std::string{},
        db::tasks.seeded = false));
    for (const auto& [key, value] : values) {
      database.execute_text(
          "INSERT INTO task_field_values (task_id, task_version, field_key, value) "
          "VALUES ($1, 1, $2, to_jsonb($3::text))",
          {id, key, value});
    }
    record_audit(database, actor.kind, actor.id, "panel.task_form", "task.draft_created", id);
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return id;
}

}  // namespace cms::api
