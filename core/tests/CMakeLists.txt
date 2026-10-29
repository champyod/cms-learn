find_package(Catch2 3 CONFIG QUIET)
if(NOT Catch2_FOUND)
  message(STATUS "Catch2 not found; core tests are skipped (install the 'tests' manifest feature)")
  return()
endif()

add_executable(cms_core_tests core_test.cpp)
target_link_libraries(cms_core_tests PRIVATE cms::core Catch2::Catch2WithMain)
cms_apply_warnings(cms_core_tests PRIVATE)
cms_apply_hardening(cms_core_tests)
cms_apply_sanitizers(cms_core_tests)

include(Catch)
catch_discover_tests(cms_core_tests)
