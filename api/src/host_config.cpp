#include <cms/api/host_config.hpp>

#include <array>
#include <cstddef>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cms::api {
namespace {

using entry_map = std::map<std::string, std::string, std::less<>>;

constexpr std::array REQUIRED_KEYS{
    std::string_view{"CMSLEARN_LISTEN_ADDRESS"}, std::string_view{"CMSLEARN_LISTEN_PORT"},
    std::string_view{"CMSLEARN_DATABASE_URL"},   std::string_view{"CMSLEARN_SEAL_KEY"},
    std::string_view{"CMSLEARN_BOOTSTRAP_ADMIN"}, std::string_view{"CMSLEARN_TLS_MODE"},
};

constexpr std::size_t SEAL_KEY_BYTES = 32;
constexpr std::array TLS_MODES{std::string_view{"proxy"}, std::string_view{"direct"}};

auto trim(std::string_view text) -> std::string_view {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

auto read_entries(std::string_view path, entry_map& entries) -> std::optional<Error> {
  auto file = std::ifstream{std::string{path}};
  if (!file) {
    return Error::make(ErrorCode::unavailable,
                       "cannot read CMSLEARN_HOST_FILE at " + std::string{path});
  }
  auto line = std::string{};
  auto number = std::size_t{0};
  while (std::getline(file, line)) {
    ++number;
    const auto trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }
    const auto separator = trimmed.find('=');
    if (separator == std::string_view::npos) {
      return Error::make(ErrorCode::invalid_argument,
                         std::string{path} + ":" + std::to_string(number) + " is not KEY=VALUE");
    }
    const auto key = std::string{trim(trimmed.substr(0, separator))};
    const auto value = std::string{trim(trimmed.substr(separator + 1))};
    if (!entries.emplace(key, value).second) {
      return Error::make(ErrorCode::invalid_argument,
                         std::string{path} + ":" + std::to_string(number) + " repeats " + key);
    }
  }
  return std::nullopt;
}

auto is_port(std::string_view text) -> bool {
  if (text.empty() || text.size() > 5) {
    return false;
  }
  auto value = 0;
  for (const auto character : text) {
    if (character < '0' || character > '9') {
      return false;
    }
    value = (value * 10) + (character - '0');
  }
  return value >= 1 && value <= 65535;
}

auto looks_like_url(std::string_view text) -> bool {
  return text.starts_with("postgresql://") || text.starts_with("postgres://");
}

auto is_known_tls_mode(std::string_view text) -> bool {
  for (const auto mode : TLS_MODES) {
    if (text == mode) {
      return true;
    }
  }
  return false;
}

auto value_of(const entry_map& entries, std::string_view key) -> std::string_view {
  const auto found = entries.find(key);
  return found == entries.end() ? std::string_view{} : std::string_view{found->second};
}

auto check(const entry_map& entries, std::vector<std::string>& problems) -> void {
  for (const auto key : REQUIRED_KEYS) {
    if (!entries.contains(key)) {
      problems.emplace_back("missing " + std::string{key});
    }
  }
  const auto validate = [&](std::string_view key, bool ok, std::string_view complaint) {
    if (entries.contains(key) && !ok) {
      problems.emplace_back(complaint);
    }
  };
  validate("CMSLEARN_LISTEN_ADDRESS", !value_of(entries, "CMSLEARN_LISTEN_ADDRESS").empty(),
           "CMSLEARN_LISTEN_ADDRESS is empty");
  validate("CMSLEARN_LISTEN_PORT", is_port(value_of(entries, "CMSLEARN_LISTEN_PORT")),
           "CMSLEARN_LISTEN_PORT is not a port number");
  validate("CMSLEARN_DATABASE_URL", looks_like_url(value_of(entries, "CMSLEARN_DATABASE_URL")),
           "CMSLEARN_DATABASE_URL is not a postgresql:// URL");
  validate("CMSLEARN_SEAL_KEY", value_of(entries, "CMSLEARN_SEAL_KEY").size() >= SEAL_KEY_BYTES,
           "CMSLEARN_SEAL_KEY is shorter than 32 bytes");
  validate("CMSLEARN_BOOTSTRAP_ADMIN",
           value_of(entries, "CMSLEARN_BOOTSTRAP_ADMIN").find('@') != std::string_view::npos,
           "CMSLEARN_BOOTSTRAP_ADMIN is not an email address");
  validate("CMSLEARN_TLS_MODE", is_known_tls_mode(value_of(entries, "CMSLEARN_TLS_MODE")),
           "CMSLEARN_TLS_MODE is neither proxy nor direct");
}

auto join(const std::vector<std::string>& problems) -> std::string {
  auto joined = std::string{};
  for (const auto& problem : problems) {
    if (!joined.empty()) {
      joined += "; ";
    }
    joined += problem;
  }
  return joined;
}

}  // namespace

auto load_host_config(std::string_view path) -> Result<HostConfig> {
  auto entries = entry_map{};
  if (const auto failure = read_entries(path, entries)) {
    return std::unexpected(*failure);
  }

  auto problems = std::vector<std::string>{};
  check(entries, problems);
  if (!problems.empty()) {
    return std::unexpected(
        Error::make(ErrorCode::invalid_argument, "host file rejected: " + join(problems)));
  }

  return HostConfig{
      .listen_address = entries.at("CMSLEARN_LISTEN_ADDRESS"),
      .listen_port = entries.at("CMSLEARN_LISTEN_PORT"),
      .database_url = entries.at("CMSLEARN_DATABASE_URL"),
      .seal_key = entries.at("CMSLEARN_SEAL_KEY"),
      .bootstrap_admin = entries.at("CMSLEARN_BOOTSTRAP_ADMIN"),
      .tls_mode = entries.at("CMSLEARN_TLS_MODE"),
  };
}

}  // namespace cms::api
