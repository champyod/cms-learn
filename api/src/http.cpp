#include <cms/api/http.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace cms::api {

auto html_response(std::string body, drogon::HttpStatusCode status) -> drogon::HttpResponsePtr {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(status);
  response->setContentTypeString("text/html; charset=utf-8");
  response->setBody(std::move(body));
  return response;
}

auto text_response(std::string body, drogon::HttpStatusCode status) -> drogon::HttpResponsePtr {
  auto response = drogon::HttpResponse::newHttpResponse();
  response->setStatusCode(status);
  response->setContentTypeString("text/plain; charset=utf-8");
  response->setBody(std::move(body));
  return response;
}

auto redirect(std::string_view location) -> drogon::HttpResponsePtr {
  return drogon::HttpResponse::newRedirectionResponse(std::string{location},
                                                     drogon::k303SeeOther);
}

}  // namespace cms::api
