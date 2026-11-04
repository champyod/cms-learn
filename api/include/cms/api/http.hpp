#pragma once

#include <string>
#include <string_view>

#include <drogon/drogon.h>

namespace cms::api {

/// @brief An HTML response with @p status.
[[nodiscard]] auto html_response(std::string body, drogon::HttpStatusCode status = drogon::k200OK)
    -> drogon::HttpResponsePtr;

/// @brief A plain-text response with @p status.
[[nodiscard]] auto text_response(std::string body,
                                 drogon::HttpStatusCode status = drogon::k200OK)
    -> drogon::HttpResponsePtr;

/// @brief A 303 redirect to @p location.
[[nodiscard]] auto redirect(std::string_view location) -> drogon::HttpResponsePtr;

}  // namespace cms::api
