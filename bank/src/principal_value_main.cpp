#include <cms/bank/visibility.hpp>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include <drogon/orm/DbClient.h>

#include <cms/db/connection.hpp>

namespace {

constexpr std::string_view USAGE =
    "usage: cms_principal_value <command>\n"
    "  encode <kind> <id>              print the encoded audience value\n"
    "  decode <value>                  print the <kind> and <id> value carries\n"
    "  everyone                        print the value that grants a task to everyone\n"
    "  limit                           print the largest identifier a kind can carry\n"
    "  serial <principal-id>           print the principal's numeric serial id\n"
    "  audience <principal-id>         print the request audience, space separated\n"
    "  default <learner|administrator> print the rows a new task carries, space separated";

constexpr const char* DATABASE_VARIABLE = "CMSLEARN_DATABASE_URL";
constexpr std::string_view DATABASE_HINT =
    "CMSLEARN_DATABASE_URL is not set; serial and audience read the database";

auto fail(std::string_view message) noexcept -> int {
  try {
    std::println(stderr, "cms-principal-value: {}", message);
  } catch (...) {
    constexpr std::string_view PREFIX = "cms-principal-value: ";
    std::fwrite(PREFIX.data(), 1, PREFIX.size(), stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
  }
  return 1;
}

auto parse_number(std::string_view text, std::int64_t& out) -> bool {
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto parsed = std::from_chars(begin, end, out);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

auto parse_kind(std::string_view text) -> std::optional<cms::bank::AudienceKind> {
  if (text == "everyone") {
    return cms::bank::AudienceKind::everyone;
  }
  if (text == "learner") {
    return cms::bank::AudienceKind::learner;
  }
  if (text == "administrator") {
    return cms::bank::AudienceKind::administrator;
  }
  if (text == "team") {
    return cms::bank::AudienceKind::team;
  }
  return std::nullopt;
}

auto name(cms::bank::AudienceKind kind) -> std::string_view {
  switch (kind) {
    case cms::bank::AudienceKind::everyone:
      return "everyone";
    case cms::bank::AudienceKind::learner:
      return "learner";
    case cms::bank::AudienceKind::administrator:
      return "administrator";
    case cms::bank::AudienceKind::team:
      return "team";
  }
  return "unknown";
}

auto print_values(const std::vector<std::int64_t>& values) -> void {
  for (std::size_t index = 0; index < values.size(); ++index) {
    std::print("{}{}", index == 0 ? "" : " ", values[index]);
  }
  std::print("\n");
}

auto database_from_environment() -> std::optional<cms::db::connection> {
  const auto* dsn = std::getenv(DATABASE_VARIABLE);
  if (dsn == nullptr || std::strlen(dsn) == 0) {
    return std::nullopt;
  }
  static const auto client = ::drogon::orm::DbClient::newPgClient(std::string{dsn}, 1);
  return cms::db::connection{std::make_shared<const cms::db::connection_config>(
      cms::db::connection_config{.client = client})};
}

auto run(int argc, char** argv) -> int {
  if (argc < 2) {
    return fail(USAGE);
  }
  const auto command = std::string_view{argv[1]};
  if (command == "everyone" && argc == 2) {
    std::println("{}", cms::bank::everyone());
    return 0;
  }
  if (command == "limit" && argc == 2) {
    std::println("{}", cms::bank::identifier_limit());
    return 0;
  }
  if (command == "encode" && argc == 4) {
    const auto kind = parse_kind(argv[2]);
    auto id = std::int64_t{0};
    if (!kind || !parse_number(argv[3], id)) {
      return fail("encode needs a kind (everyone, learner, administrator, team) and an integer id");
    }
    const auto value = cms::bank::encode(*kind, id);
    if (!value) {
      return fail(value.error().message);
    }
    std::println("{}", *value);
    return 0;
  }
  if (command == "decode" && argc == 3) {
    auto value = std::int64_t{0};
    if (!parse_number(argv[2], value)) {
      return fail("decode needs an integer value");
    }
    const auto principal = cms::bank::decode(value);
    if (!principal) {
      return fail(principal.error().message);
    }
    std::println("{} {}", name(principal->kind), principal->id);
    return 0;
  }
  if (command == "serial" && argc == 3) {
    auto database = database_from_environment();
    if (!database) {
      return fail(DATABASE_HINT);
    }
    const auto serial = cms::bank::serial_id_of(*database, argv[2]);
    if (!serial) {
      return fail(serial.error().message);
    }
    std::println("{}", *serial);
    return 0;
  }
  if (command == "audience" && argc == 3) {
    auto database = database_from_environment();
    if (!database) {
      return fail(DATABASE_HINT);
    }
    const auto audience = cms::bank::request_audience(*database, argv[2]);
    if (!audience) {
      return fail(audience.error().message);
    }
    print_values(*audience);
    return 0;
  }
  if (command == "default" && argc == 3) {
    const auto author = std::string_view{argv[2]};
    if (author != "learner" && author != "administrator") {
      return fail("default needs learner or administrator");
    }
    const auto rows = cms::bank::default_visibility(author == "learner"
                                                        ? cms::bank::AuthorKind::learner
                                                        : cms::bank::AuthorKind::administrator);
    print_values(rows);
    return 0;
  }
  return fail(USAGE);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    return fail(error.what());
  } catch (...) {
    return fail("the command stopped on a failure that is not a std::exception");
  }
}
