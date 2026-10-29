cmake_minimum_required(VERSION 3.28)
project(cms_learn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectSettings)

# Services stay opt-in so a checkout configures before vcpkg has installed anything, and the
# dependency-free core still compiles on a machine that has only a compiler.
option(CMS_WITH_API "Build the HTTP service (Drogon, Inja)" OFF)
option(CMS_WITH_JUDGE "Build the judge (isolate, libsodium, spdlog)" OFF)
option(CMS_BUILD_TESTS "Build the C++ test binaries (requires the 'tests' manifest feature)" ON)

add_subdirectory(core)

if(CMS_WITH_API)
  add_subdirectory(api)
endif()

if(CMS_WITH_JUDGE)
  add_subdirectory(judge)
endif()
