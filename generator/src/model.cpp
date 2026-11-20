#include <cms/generate/model.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>
#include <cms/seal.hpp>

namespace cms::generate {
namespace {

constexpr std::string_view NO_RECORD =
    "no enabled model record: an operator has to add one on the models page and pick it as the default";

}  // namespace

auto read_effective_model(cms::db::connection& database) -> Result<ModelRecord> {
  try {
    // The effective record is the enabled default, and the partial unique index makes that at most one row. A disabled
    // default is not effective: an operator turning it off is asking the worker to stop calling it.
    for (const auto& row : database(sqlpp::select(db::models.name, db::models.baseUrl, db::models.protocol,
                                                  db::models.modelId, db::models.sealedKey)
                                        .from(db::models)
                                        .where(db::models.enabled == true && db::models.isDefault == true))) {
      return ModelRecord{.name = std::string{row.name},
                         .base_url = std::string{row.baseUrl},
                         .protocol = std::string{row.protocol},
                         .model_id = std::string{row.modelId},
                         .sealed_key = std::string{row.sealedKey}};
    }
  } catch (const std::exception& error) {
    return std::unexpected(Error::make(ErrorCode::unavailable, error.what()));
  }
  return std::unexpected(Error::make(ErrorCode::not_found, NO_RECORD));
}

auto make_model_client(const ModelRecord& record, std::string_view prompt_version, std::int64_t seed)
    -> Result<std::unique_ptr<ModelClient>> {
  const auto sealer = seal::sealer();
  if (!sealer) {
    return std::unexpected(sealer.error());
  }
  // The key leaves the row only through this call: it is opened with the deployment's key and handed to the adapter, and
  // it never appears in a refusal or a log line.
  const auto key = sealer->unseal(record.sealed_key);
  if (!key) {
    return std::unexpected(Error::make(key.error().code,
                                       "the model record \"" + record.name + "\" cannot be used: " + key.error().message));
  }
  if (record.protocol == OPENAI_CHAT_PROTOCOL) {
    return make_openai_chat_client(record, *key, prompt_version, seed);
  }
  return std::unexpected(Error::make(ErrorCode::invalid_argument,
                                     "the model record \"" + record.name + "\" names the protocol \"" +
                                         record.protocol + "\", which this build has no adapter for"));
}

}  // namespace cms::generate

namespace cms::generate {

RecordBackedClient::RecordBackedClient(std::shared_ptr<const cms::db::connection_config> config,
                                       std::string prompt_version, std::int64_t seed)
    : config_(std::move(config)), prompt_version_(std::move(prompt_version)), seed_(seed) {}

auto RecordBackedClient::complete(std::string_view prompt) -> Result<Completion> {
  if (adapter_ == nullptr) {
    auto database = cms::db::connection{*config_};
    const auto record = read_effective_model(database);
    if (!record) {
      return std::unexpected(record.error());
    }
    auto built = make_model_client(*record, prompt_version_, seed_);
    if (!built) {
      return std::unexpected(built.error());
    }
    adapter_ = std::move(*built);
  }
  return adapter_->complete(prompt);
}

}  // namespace cms::generate
