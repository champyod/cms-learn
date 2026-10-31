#include <cms/api/host_config.hpp>

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view COMPLETE_FILE = R"(CMSLEARN_LISTEN_ADDRESS=127.0.0.1
CMSLEARN_LISTEN_PORT=8080
CMSLEARN_DATABASE_URL=postgresql://cmslearn@database/cmslearn
CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef
CMSLEARN_BOOTSTRAP_ADMIN=operator@example.org
CMSLEARN_TLS_MODE=proxy
CMSLEARN_ADMIN_SOCKET=/run/cmslearn/admin.sock
CMSLEARN_ADMIN_UID=1000
)";

/// Writes a host file for one test and removes it afterwards.
class ScratchFile {
 public:
  explicit ScratchFile(std::string_view contents) {
    static auto counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("cms-host-config-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) + ".env");
    auto file = std::ofstream{path_};
    file << contents;
  }

  ~ScratchFile() {
    auto error = std::error_code{};
    std::filesystem::remove(path_, error);
  }

  ScratchFile(const ScratchFile&) = delete;
  ScratchFile& operator=(const ScratchFile&) = delete;
  ScratchFile(ScratchFile&&) = delete;
  ScratchFile& operator=(ScratchFile&&) = delete;

  [[nodiscard]] std::string path() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST_CASE("a complete host file is accepted and every fact is carried", "[host-config]") {
  const auto file = ScratchFile{COMPLETE_FILE};
  const auto config = cms::api::load_host_config(file.path());

  REQUIRE(config.has_value());
  CHECK(config->listen_address == "127.0.0.1");
  CHECK(config->listen_port == "8080");
  CHECK(config->database_url == "postgresql://cmslearn@database/cmslearn");
  CHECK(config->seal_key.size() == 32);
  CHECK(config->bootstrap_admin == "operator@example.org");
  CHECK(config->tls_mode == "proxy");
  CHECK(config->admin_socket == "/run/cmslearn/admin.sock");
  CHECK(config->admin_uid == "1000");
}

TEST_CASE("a libpq keyword connection string is accepted", "[host-config]") {
  auto text = std::string{COMPLETE_FILE};
  const auto from = text.find("postgresql://cmslearn@database/cmslearn");
  REQUIRE(from != std::string::npos);
  text.replace(from, 39, "host=/run/postgresql dbname=cmslearn");
  const auto file = ScratchFile{text};

  const auto config = cms::api::load_host_config(file.path());
  REQUIRE(config.has_value());
  CHECK(config->database_url == "host=/run/postgresql dbname=cmslearn");
}

TEST_CASE("a missing fact is refused and named", "[host-config]") {
  const auto file = ScratchFile{"CMSLEARN_LISTEN_ADDRESS=127.0.0.1\n"};
  const auto config = cms::api::load_host_config(file.path());

  REQUIRE(!config.has_value());
  CHECK(config.error().message.find("CMSLEARN_SEAL_KEY") != std::string::npos);
}

TEST_CASE("a refused host file never repeats a value", "[host-config]") {
  auto text = std::string{COMPLETE_FILE};
  const auto from = text.find("CMSLEARN_SEAL_KEY=0123456789abcdef0123456789abcdef");
  REQUIRE(from != std::string::npos);
  text.replace(from + 18, 32, "distinctive-secret-value");
  const auto file = ScratchFile{text};

  const auto config = cms::api::load_host_config(file.path());
  REQUIRE(!config.has_value());
  CHECK(config.error().message.find("shorter than 32 bytes") != std::string::npos);
  CHECK(config.error().message.find("distinctive-secret-value") == std::string::npos);
}
