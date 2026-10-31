add_test(
  NAME api_host_config
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/host_config_test.sh" "$<TARGET_FILE:cms_api>")

add_test(
  NAME api_settings_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/settings_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_settings_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
