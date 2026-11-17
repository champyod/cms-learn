#include <cms/generate/artefact.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>
#include <cms/db/transaction.hpp>

namespace cms::generate {
namespace {

constexpr std::string_view NO_ARTEFACT = "a step recorded no artefact";

auto refused(std::string_view message) -> Error {
  return Error::make(ErrorCode::invalid_argument, message);
}

/// Hashes the bytes and returns both the address and, for a store artefact, the key the bytes were written to.
auto address_of(cms::store::ObjectStore& store, const Artefact& artefact) -> Result<std::string> {
  if (artefact.home == ArtefactHome::row) {
    return cms::store::digest_of(artefact.bytes);
  }
  if (artefact.object_key.empty()) {
    return std::unexpected(refused("the store artefact for step " + std::to_string(artefact.step) +
                                   " names no object key"));
  }
  return store.write(artefact.object_key, artefact.bytes);
}

/// One row per model call (ADR 0030). Recording the same call again replaces the row it already wrote — a step
/// re-run must not leave two rows naming one call — while a second call at the same step, kind and locale is a
/// second row, which the unique key on (job, step, kind, locale, call_index) states.
constexpr std::string_view RECORD_ARTEFACT = R"(
INSERT INTO generation_artefacts (job_id, step, kind, locale, object_key, inline_text, digest, model_id,
                                  prompt_version, seed, call_index)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
ON CONFLICT (job_id, step, kind, locale, call_index) DO UPDATE
SET object_key = excluded.object_key, inline_text = excluded.inline_text, digest = excluded.digest,
    model_id = excluded.model_id, prompt_version = excluded.prompt_version, seed = excluded.seed
)";

auto text_or_null(const std::string& value) -> cms::db::bound_value {
  return value.empty() ? cms::db::bound_value{std::nullptr_t{}} : cms::db::bound_value{value};
}

auto insert_artefact(cms::db::connection& database, std::int64_t job_id, const Artefact& artefact,
                     const std::string& digest) -> void {
  const auto is_store = artefact.home == ArtefactHome::store;
  database.execute_text(
      std::string{RECORD_ARTEFACT},
      {job_id,
       static_cast<std::int64_t>(artefact.step),
       artefact.kind,
       text_or_null(artefact.locale),
       is_store ? cms::db::bound_value{artefact.object_key} : cms::db::bound_value{std::nullptr_t{}},
       is_store ? cms::db::bound_value{std::nullptr_t{}} : cms::db::bound_value{artefact.bytes},
       digest,
       artefact.call.model_id,
       artefact.call.prompt_version,
       static_cast<std::int64_t>(artefact.call.seed),
       static_cast<std::int64_t>(artefact.call.call_index)});
}

struct ArtefactRow {
  std::optional<std::string> object_key;
  std::optional<std::string> inline_text;
  std::string digest;
};

/// @return The newest row of @p step and @p kind, taking the first whose locale is @p locale when one is asked for.
auto read_row(cms::db::connection& database, std::int64_t job_id, std::int16_t step, std::string_view kind,
              std::string_view locale) -> Result<ArtefactRow> {
  auto found = std::optional<ArtefactRow>{};
  for (const auto& row : database(sqlpp::select(cms::db::generationArtefacts.locale,
                                               cms::db::generationArtefacts.objectKey,
                                               cms::db::generationArtefacts.inlineText,
                                               cms::db::generationArtefacts.digest)
                                       .from(cms::db::generationArtefacts)
                                       .where(cms::db::generationArtefacts.jobId == job_id &&
                                              cms::db::generationArtefacts.step ==
                                                  static_cast<std::int64_t>(step) &&
                                              cms::db::generationArtefacts.kind == std::string{kind})
                                       .order_by(cms::db::generationArtefacts.id.desc()))) {
    const auto row_locale =
        row.locale.has_value() ? std::optional<std::string>{std::string{*row.locale}} : std::nullopt;
    if (!locale.empty() && row_locale != std::optional<std::string>{std::string{locale}}) {
      continue;
    }
    found = ArtefactRow{.object_key = row.objectKey.has_value()
                                          ? std::optional<std::string>{std::string{*row.objectKey}}
                                          : std::nullopt,
                        .inline_text = row.inlineText.has_value()
                                           ? std::optional<std::string>{std::string{*row.inlineText}}
                                           : std::nullopt,
                        .digest = std::string{row.digest}};
    break;
  }
  if (!found) {
    const auto wanted = locale.empty() ? std::string{} : " and locale \"" + std::string{locale} + "\"";
    return std::unexpected(Error::make(ErrorCode::not_found,
                                       "no artefact of step " + std::to_string(step) + " and kind \"" +
                                           std::string{kind} + "\"" + wanted + " for job " +
                                           std::to_string(job_id)));
  }
  return *found;
}

auto verify_inline(const ArtefactRow& row) -> Result<std::string> {
  const auto stored = cms::store::digest_of(*row.inline_text);
  if (stored != row.digest) {
    return std::unexpected(Error::make(ErrorCode::conflict,
                                       "the inline artefact does not match the digest its row records: the row "
                                       "says " +
                                           row.digest + ", the stored text hashes to " + stored));
  }
  return *row.inline_text;
}

}  // namespace

auto MemoryArtefactSink::record(std::int64_t job_id, const std::vector<Artefact>& artefacts)
    -> Result<std::size_t> {
  if (artefacts.empty()) {
    return std::unexpected(refused(NO_ARTEFACT));
  }
  job_id_ = job_id;
  digests_.clear();
  for (const auto& artefact : artefacts) {
    digests_.push_back(cms::store::digest_of(artefact.bytes));
  }
  return artefacts.size();
}

SqlArtefactSink::SqlArtefactSink(::drogon::orm::DbClient& client, cms::store::ObjectStore& store)
    : client_(client), store_(store) {}

auto SqlArtefactSink::record(std::int64_t job_id, const std::vector<Artefact>& artefacts)
    -> Result<std::size_t> {
  if (artefacts.empty()) {
    return std::unexpected(refused(NO_ARTEFACT));
  }
  auto digests = std::vector<std::string>{};
  digests.reserve(artefacts.size());
  for (const auto& artefact : artefacts) {
    auto digest = address_of(store_, artefact);
    if (!digest) {
      return std::unexpected(digest.error());
    }
    digests.push_back(std::move(*digest));
  }
  try {
    auto transaction = cms::db::Transaction{client_};
    auto& database = transaction.connection();
    for (std::size_t index = 0; index < artefacts.size(); ++index) {
      insert_artefact(database, job_id, artefacts[index], digests[index]);
    }
    transaction.commit();
    return artefacts.size();
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

auto read_artefact(::drogon::orm::DbClient& client, const cms::store::ObjectStore& store,
                   std::int64_t job_id, std::int16_t step, std::string_view kind, std::string_view locale)
    -> Result<StoredArtefact> {
  try {
    auto transaction = cms::db::Transaction{client};
    const auto row = read_row(transaction.connection(), job_id, step, kind, locale);
    if (!row) {
      return std::unexpected(row.error());
    }
    auto payload = row->object_key.has_value()
                       ? store.read_verified(*row->object_key, row->digest)
                       : verify_inline(*row);
    if (!payload) {
      return std::unexpected(payload.error());
    }
    transaction.commit();
    return StoredArtefact{.digest = row->digest, .bytes = std::move(*payload)};
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
}

}  // namespace cms::generate
