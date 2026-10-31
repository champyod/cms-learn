# The loader is tested through its own interface rather than through the binary, so main carries only
# the paths a real invocation takes.
find_package(Catch2 3 CONFIG QUIET)
if(Catch2_FOUND)
  add_executable(cms_api_config_tests config_test.cpp)
  target_link_libraries(cms_api_config_tests PRIVATE cms::api_config Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_config_tests PRIVATE)
  cms_apply_hardening(cms_api_config_tests)
  cms_apply_sanitizers(cms_api_config_tests)

  include(Catch)
  catch_discover_tests(cms_api_config_tests)

  add_executable(cms_api_mail_tests mailer_test.cpp)
  target_link_libraries(cms_api_mail_tests PRIVATE cms::api_mail Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_mail_tests PRIVATE)
  cms_apply_hardening(cms_api_mail_tests)
  cms_apply_sanitizers(cms_api_mail_tests)
  catch_discover_tests(cms_api_mail_tests)

  add_executable(cms_api_identity_tests uuid_test.cpp)
  target_link_libraries(cms_api_identity_tests PRIVATE cms::api_identity Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_identity_tests PRIVATE)
  cms_apply_hardening(cms_api_identity_tests)
  cms_apply_sanitizers(cms_api_identity_tests)
  catch_discover_tests(cms_api_identity_tests)
endif()

add_test(
  NAME api_host_config
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/host_config_test.sh" "$<TARGET_FILE:cms_api>")

add_test(
  NAME api_settings_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/settings_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_settings_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
