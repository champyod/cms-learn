#include <cms/generate/model.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <curl/curl.h>
#include <simdjson.h>

namespace cms::generate {
namespace {

constexpr long CONNECT_TIMEOUT_SECONDS = 10;
constexpr long CALL_TIMEOUT_SECONDS = 120;

struct CurlDeleter {
  void operator()(CURL* handle) const { curl_easy_cleanup(handle); }
};

struct ListDeleter {
  void operator()(curl_slist* list) const { curl_slist_free_all(list); }
};

auto collect(char* data, std::size_t size, std::size_t count, void* user) -> std::size_t {
  const auto bytes = size * count;
  static_cast<std::string*>(user)->append(data, bytes);
  return bytes;
}

/// @return @p text as a JSON string body, quotes and backslashes escaped, so a prompt cannot break the request.
auto json_string(std::string_view text) -> std::string {
  auto out = std::string{"\""};
  for (const auto character : text) {
    switch (character) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += character;
        break;
    }
  }
  out += '"';
  return out;
}

/// @brief The OpenAI-compatible chat client: one POST per call to `<base>/chat/completions`.
class OpenAiChatClient final : public ModelClient {
 public:
  OpenAiChatClient(std::string url, std::string key, std::string model_id, std::string prompt_version,
                   std::int64_t seed)
      : url_(std::move(url)),
        key_(std::move(key)),
        model_id_(std::move(model_id)),
        prompt_version_(std::move(prompt_version)),
        seed_(seed) {}

  auto complete(std::string_view prompt) -> Result<Completion> override {
    const auto body = "{\"model\":" + json_string(model_id_) + ",\"seed\":" + std::to_string(seed_) +
                      ",\"messages\":[{\"role\":\"user\",\"content\":" + json_string(prompt) + "}]}";
    auto handle = std::unique_ptr<CURL, CurlDeleter>{curl_easy_init()};
    if (!handle) {
      return std::unexpected(Error::make(ErrorCode::unavailable, "libcurl could not create a handle"));
    }
    auto headers = std::unique_ptr<curl_slist, ListDeleter>{
        curl_slist_append(nullptr, ("Authorization: Bearer " + key_).c_str())};
    headers.reset(curl_slist_append(headers.release(), "Content-Type: application/json"));
    auto response = std::string{};
    curl_easy_setopt(handle.get(), CURLOPT_URL, url_.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, CALL_TIMEOUT_SECONDS);
    const auto status = curl_easy_perform(handle.get());
    if (status != CURLE_OK) {
      return std::unexpected(Error::make(ErrorCode::unavailable, "the model call failed: " +
                                                                   std::string{curl_easy_strerror(status)}));
    }
    auto length = long{0};
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &length);
    if (length < 200 || length > 299) {
      // The response body can carry the vendor's own message, never our key: it is not part of this string.
      return std::unexpected(Error::make(ErrorCode::unavailable,
                                         "the model endpoint answered HTTP " + std::to_string(length)));
    }
    const auto text = text_of(response);
    if (!text) {
      return std::unexpected(text.error());
    }
    ++call_index_;
    return Completion{.text = *text,
                      .call = ModelCall{.model_id = model_id_,
                                        .prompt_version = prompt_version_,
                                        .seed = seed_,
                                        .call_index = call_index_}};
  }

 private:
  static auto text_of(const std::string& body) -> Result<std::string> {
    auto document = simdjson::dom::parser{};
    auto parsed = simdjson::dom::element{};
    if (document.parse(body).get(parsed) != simdjson::SUCCESS) {
      return std::unexpected(Error::make(ErrorCode::unavailable, "the model endpoint answered with a body that is not JSON"));
    }
    auto view = std::string_view{};
    if (parsed.at_pointer("/choices/0/message/content").get_string().get(view) != simdjson::SUCCESS) {
      return std::unexpected(
          Error::make(ErrorCode::unavailable, "the model endpoint answered without choices[0].message.content"));
    }
    return std::string{view};
  }

  std::string url_;
  std::string key_;
  std::string model_id_;
  std::string prompt_version_;
  std::int64_t seed_{0};
  std::int32_t call_index_{0};
};

}  // namespace

auto make_openai_chat_client(const ModelRecord& record, std::string_view key, std::string_view prompt_version,
                             std::int64_t seed) -> Result<std::unique_ptr<ModelClient>> {
  auto base = std::string{record.base_url};
  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  return std::unique_ptr<ModelClient>{std::make_unique<OpenAiChatClient>(
      base + "/chat/completions", std::string{key}, record.model_id, std::string{prompt_version}, seed)};
}

}  // namespace cms::generate
