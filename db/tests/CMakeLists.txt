find_package(Catch2 3 CONFIG QUIET)
if(NOT Catch2_FOUND)
  message(STATUS "Catch2 not found; db tests are skipped (install the 'tests' manifest feature)")
  return()
endif()

add_executable(cms_db_tests db_test.cpp)
target_link_libraries(cms_db_tests PRIVATE cms::db Catch2::Catch2WithMain)
cms_apply_warnings(cms_db_tests PRIVATE)
cms_apply_hardening(cms_db_tests)
cms_apply_sanitizers(cms_db_tests)

include(Catch)
catch_discover_tests(cms_db_tests)

# The connector needs Drogon, so its tests exist only where the component does.
if(TARGET cms::db_drogon)
  add_executable(cms_db_connection_tests connection_test.cpp)
  target_link_libraries(cms_db_connection_tests PRIVATE cms::db_drogon Catch2::Catch2WithMain)
  cms_apply_warnings(cms_db_connection_tests PRIVATE)
  cms_apply_hardening(cms_db_connection_tests)
  cms_apply_sanitizers(cms_db_connection_tests)

  catch_discover_tests(cms_db_connection_tests)
endif()
