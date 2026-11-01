#pragma once

#include <string>
#include <string_view>

namespace cms::api {

/// @brief A random version-4 UUID in lower-case canonical form, 36 characters.
///
/// The schema declares `gen_random_uuid()` as the default, and sqlpp23 0.70 cannot express
/// `INSERT … RETURNING`, so the caller has to supply identity before the row exists. A session id is
/// also the opaque value a cookie carries, so this uses libsodium's CSPRNG rather than `std::random`.
[[nodiscard]] std::string uuid_v4();

/// @brief Whether @p text has the canonical shape: 36 characters, hex digits in five dash-separated groups.
///
/// A session id arrives from a cookie, so it is client input. A value that is not this shape cannot name a
/// session, and letting it reach a `uuid` column turns a refusal into the server's type error, which is both
/// the wrong answer and a message a client should never see.
[[nodiscard]] bool has_uuid_shape(std::string_view text);

}  // namespace cms::api
