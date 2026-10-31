#pragma once

#include <string>
#include <string_view>

#include <cms/db/connection.hpp>
#include <cms/settings_snapshot.hpp>

namespace cms::api {

/// @brief Answers one line of the control protocol.
///
/// @param request One JSON object, newline optional.
/// @param settings The snapshot the answer is computed against.
/// @param database The connection the action reads or writes through.
/// @return One JSON object, newline-terminated. A malformed request, an unknown action and a refused
/// action all produce `"ok": false` with a reason, because a client that gets no answer learns nothing.
///
/// Kept apart from the socket so the whole protocol is testable without opening a file descriptor: the
/// socket's only job is to carry these bytes to a client the peer credentials already authorized.
std::string dispatch_line(std::string_view request, const SettingsSnapshot& settings,
                          db::connection& database);

}  // namespace cms::api
