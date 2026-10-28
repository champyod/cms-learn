cmake_minimum_required(VERSION 3.28)
project(cms_learn_cpp LANGUAGES CXX)

# One root for every C++ translation unit this repo ships: the reference solutions today, the api and
# judge targets when they land. It exists because the safety flags must be declared once — a gate that
# re-declares them in a script ends up testing a configuration nobody ships.
#
# The gate is `bun run check:cpp`; these are the underlying commands:
#   cmake --preset safety && cmake --build --preset safety && ctest --preset safety
#   cmake --preset tidy   && clang-tidy -p build/tidy <files>

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Sanitizers)

enable_testing()

add_subdirectory(references)
