#include <cms/api/audit.hpp>

#include <string>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::api {

void record_audit(db::connection& database, std::string_view actor_id, std::string_view invoked_by,
                  std::string_view action, std::string_view subject) {
  database(sqlpp::insert_into(db::auditEvents).set(
      db::auditEvents.actorId = std::string{actor_id},
      db::auditEvents.invokedBy = std::string{invoked_by},
      db::auditEvents.action = std::string{action},
      db::auditEvents.subject = std::string{subject}));
}

}  // namespace cms::api
