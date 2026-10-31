#include <cms/api/host_config.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

namespace {

constexpr std::string_view CHECK_FLAG = "--check";

auto fail(std::string_view message) -> int {
  std::cerr << "cms-api: " << message << '\n';
  return 1;
}

auto load_from_environment() -> cms::Result<cms::api::HostConfig> {
  const auto* path = std::getenv("CMSLEARN_HOST_FILE");
  if (path == nullptr || std::strlen(path) == 0) {
    return std::unexpected(cms::Error::make(
        cms::ErrorCode::invalid_argument,
        "CMSLEARN_HOST_FILE is not set; this service takes every fact from the file it names"));
  }
  return cms::api::load_host_config(path);
}

}  // namespace

int main(int argc, char** argv) {
  const auto config = load_from_environment();
  if (!config) {
    return fail(config.error().message);
  }

  if (argc > 1 && std::string_view{argv[1]} == CHECK_FLAG) {
    std::cout << "cms-api: configuration is valid for " << config->listen_address << ':'
              << config->listen_port << " (tls " << config->tls_mode << ")\n";
    return 0;
  }

  auto& app = drogon::app();
  app.addListener(config->listen_address, static_cast<std::uint16_t>(std::stoi(config->listen_port)));
  app.run();
  return 0;
}
