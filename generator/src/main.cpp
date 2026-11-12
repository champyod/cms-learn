#include <cms/generate/config.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr int CONFIG_REFUSED = 2;
constexpr int NO_WORK = 0;

}  // namespace

/// The generation worker. This step lays the substrate: it proves its configuration is the deployer's and that it
/// can start; the seven steps themselves arrive on top of it. A missing value names the key and never prints one.
auto main(int argc, char** argv) -> int {
  const auto check_only = argc > 1 && std::string{argv[1]} == "--check";
  const auto config = cms::generate::read_model_config(cms::generate::environment_lookup());
  if (!config) {
    std::cerr << config.error().message << "\n";
    return CONFIG_REFUSED;
  }
  if (!check_only) {
    std::cout << "cms_generate: configuration accepted; no step is wired yet\n";
  }
  return NO_WORK;
}
