#include <cms/api/audit.hpp>

#include <string>

#include <sqlpp23/sqlpp23.h>

#include <cms/db/tables.hpp>

namespace cms::api {

void record_audit(db::connection& database, std::string_view actor_kind, std::string_view actor_id,
                  std::string_view invoked_by, std::string_view action, std::string_view subject) {
  // The table's own CHECK refuses a row whose id column does not match its kind, so the kind is
  // matched once here and the row is written with only that column set.
  if (actor_kind == "learner") {
    database(sqlpp::insert_into(db::auditEvents).set(
        db::auditEvents.actorKind = std::string{actor_kind},
        db::auditEvents.learnerActorId = std::string{actor_id},
        db::auditEvents.invokedBy = std::string{invoked_by},
        db::auditEvents.action = std::string{action}, db::auditEvents.subject = std::string{subject}));
  } else if (actor_kind == "admin") {
    database(sqlpp::insert_into(db::auditEvents).set(
        db::auditEvents.actorKind = std::string{actor_kind},
        db::auditEvents.adminActorId = std::string{actor_id},
        db::auditEvents.invokedBy = std::string{invoked_by},
        db::auditEvents.action = std::string{action}, db::auditEvents.subject = std::string{subject}));
  } else {
    database(sqlpp::insert_into(db::auditEvents).set(
        db::auditEvents.actorKind = std::string{actor_kind},
        db::auditEvents.systemActorId = std::string{actor_id},
        db::auditEvents.invokedBy = std::string{invoked_by},
        db::auditEvents.action = std::string{action}, db::auditEvents.subject = std::string{subject}));
  }
}

}  // namespace cms::api
