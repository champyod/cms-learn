cmake_minimum_required(VERSION 3.28)
project(cms_learn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectSettings)
include(Sanitizers)

# The runtime stage's libstdc++6 is GCC 14's, so a dynamically linked GCC-16 binary would fail on GLIBCXX versions.
if(NOT CMS_LEARN_SANITIZE)
  add_link_options(-static-libstdc++ -static-libgcc)
endif()

enable_testing()

option(CMS_WITH_API "Build the HTTP service (Drogon, Inja)" OFF)
option(CMS_WITH_JUDGE "Build the judge (isolate, libsodium, spdlog)" OFF)
option(CMS_BUILD_TESTS "Build the C++ test binaries (requires the 'tests' manifest feature)" ON)

add_subdirectory(core)
add_subdirectory(db)
add_subdirectory(references)
add_subdirectory(bank)

if(CMS_WITH_API)
  add_subdirectory(api)
endif()

if(CMS_WITH_JUDGE)
  add_subdirectory(judge)
endif()
