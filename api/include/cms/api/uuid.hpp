#pragma once

#include <string>

namespace cms::api {

/// @brief A random version-4 UUID in lower-case canonical form, 36 characters.
///
/// The schema declares `gen_random_uuid()` as the default, and sqlpp23 0.70 cannot express
/// `INSERT … RETURNING`, so the caller has to supply identity before the row exists. A session id is
/// also the opaque value a cookie carries, so this uses libsodium's CSPRNG rather than `std::random`.
[[nodiscard]] std::string uuid_v4();

}  // namespace cms::api
