add_test(
  NAME api_host_config
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/host_config_test.sh" "$<TARGET_FILE:cms_api>")
