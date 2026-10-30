#pragma once

#include <optional>

#include <sqlpp23/sqlpp23.h>

namespace cms::db {

struct Settings_ {
  struct Key {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(key, key);
    using data_type = ::sqlpp::text;
    using has_default = std::false_type;
  };
  struct Kind {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(kind, kind);
    using data_type = ::sqlpp::text;
    using has_default = std::false_type;
  };
  struct ValueText {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(value_text, valueText);
    using data_type = ::sqlpp::text;
    using has_default = std::false_type;
  };
  struct Tier {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(tier, tier);
    using data_type = ::sqlpp::text;
    using has_default = std::true_type;
  };
  struct Description {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(description, description);
    using data_type = ::sqlpp::text;
    using has_default = std::true_type;
  };
  struct UpdatedAt {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(updated_at, updatedAt);
    using data_type = ::sqlpp::timestamp;
    using has_default = std::true_type;
  };
  struct UpdatedBy {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(updated_by, updatedBy);
    using data_type = ::sqlpp::text;
    using has_default = std::false_type;
  };

  SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(settings, settings);
  template <typename T>
  using _table_columns =
      sqlpp::table_columns<T, Key, Kind, ValueText, Tier, Description, UpdatedAt, UpdatedBy>;
  using _required_insert_columns = sqlpp::detail::type_set<UpdatedBy>;
};

struct Sessions_ {
  struct Id {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(id, id);
    using data_type = ::sqlpp::text;
    using has_default = std::false_type;
  };
  struct PrincipalId {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(principal_id, principalId);
    using data_type = ::sqlpp::text;
    using has_default = std::false_type;
  };
  struct PrincipalKind {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(principal_kind, principalKind);
    using data_type = ::sqlpp::text;
    using has_default = std::true_type;
  };
  struct CreatedAt {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(created_at, createdAt);
    using data_type = ::sqlpp::timestamp;
    using has_default = std::true_type;
  };
  struct ExpiresAt {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(expires_at, expiresAt);
    using data_type = ::sqlpp::timestamp;
    using has_default = std::false_type;
  };
  struct RevokedAt {
    SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(revoked_at, revokedAt);
    using data_type = std::optional<::sqlpp::timestamp>;
    using has_default = std::true_type;
  };

  SQLPP_CREATE_NAME_TAG_FOR_SQL_AND_CPP(sessions, sessions);
  template <typename T>
  using _table_columns =
      sqlpp::table_columns<T, Id, PrincipalId, PrincipalKind, CreatedAt, ExpiresAt, RevokedAt>;
  using _required_insert_columns = sqlpp::detail::type_set<Id, PrincipalId, ExpiresAt>;
};

using Settings = ::sqlpp::table_t<Settings_>;
using Sessions = ::sqlpp::table_t<Sessions_>;

inline const Settings settings{};
inline const Sessions sessions{};

}  // namespace cms::db
