cmake_minimum_required(VERSION 3.28)
project(cms_learn LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectSettings)
include(Sanitizers)

enable_testing()

option(CMS_WITH_API "Build the HTTP service (Drogon, Inja)" OFF)
option(CMS_WITH_JUDGE "Build the judge (isolate, libsodium, spdlog)" OFF)
option(CMS_BUILD_TESTS "Build the C++ test binaries (requires the 'tests' manifest feature)" ON)

add_subdirectory(core)
add_subdirectory(db)
add_subdirectory(references)

if(CMS_WITH_API)
  add_subdirectory(api)
endif()

if(CMS_WITH_JUDGE)
  add_subdirectory(judge)
endif()
