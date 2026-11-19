#include <cms/judge/promote.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <drogon/orm/DbClient.h>
#include <sqlpp23/sqlpp23.h>

#include <cms/db/connection.hpp>
#include <cms/db/tables.hpp>

namespace cms::judge {

SqlCertificateSource::SqlCertificateSource(std::shared_ptr<::drogon::orm::DbClient> client)
    : client_(std::move(client)) {}

auto SqlCertificateSource::latest(std::string_view task_id, std::int64_t task_version) const
    -> Result<std::optional<Certificate>> {
  try {
    auto database = cms::db::connection{std::make_shared<const cms::db::connection_config>(
        cms::db::connection_config{.client = client_})};
    for (const auto& row : database(sqlpp::select(cms::db::generationCertificates.outcome,
                                                 cms::db::generationCertificates.commitId)
                                       .from(cms::db::generationCertificates)
                                       .where(cms::db::generationCertificates.taskId == std::string{task_id} &&
                                              cms::db::generationCertificates.taskVersion == task_version)
                                       .order_by(cms::db::generationCertificates.createdAt.desc())
                                       .limit(1))) {
      return std::optional<Certificate>{
          Certificate{.outcome = std::string{row.outcome}, .commit_id = std::string{row.commitId}}};
    }
  } catch (const std::exception& failure) {
    return std::unexpected(Error::make(ErrorCode::unavailable, failure.what()));
  }
  return std::nullopt;
}

}  // namespace cms::judge
