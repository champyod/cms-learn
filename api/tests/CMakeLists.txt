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

  add_executable(cms_api_accounts_tests accounts_test.cpp)
  target_link_libraries(cms_api_accounts_tests PRIVATE cms::api_authority Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_accounts_tests PRIVATE)
  cms_apply_hardening(cms_api_accounts_tests)
  cms_apply_sanitizers(cms_api_accounts_tests)
  catch_discover_tests(cms_api_accounts_tests)

  add_executable(cms_api_admin_actions_creation_tests admin_actions_creation_test.cpp)
  target_link_libraries(cms_api_admin_actions_creation_tests
                        PRIVATE cms::api_authority Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_admin_actions_creation_tests PRIVATE)
  cms_apply_hardening(cms_api_admin_actions_creation_tests)
  cms_apply_sanitizers(cms_api_admin_actions_creation_tests)
  catch_discover_tests(cms_api_admin_actions_creation_tests)

  add_executable(cms_api_admin_actions_lifecycle_tests admin_actions_lifecycle_test.cpp)
  target_link_libraries(cms_api_admin_actions_lifecycle_tests
                        PRIVATE cms::api_authority Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_admin_actions_lifecycle_tests PRIVATE)
  cms_apply_hardening(cms_api_admin_actions_lifecycle_tests)
  cms_apply_sanitizers(cms_api_admin_actions_lifecycle_tests)
  catch_discover_tests(cms_api_admin_actions_lifecycle_tests)

  add_executable(cms_api_generation_tests generation_action_test.cpp)
  target_link_libraries(cms_api_generation_tests PRIVATE cms::api_authority cms::api_web Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_generation_tests PRIVATE)
  cms_apply_hardening(cms_api_generation_tests)
  cms_apply_sanitizers(cms_api_generation_tests)
  catch_discover_tests(cms_api_generation_tests)

  add_executable(cms_api_elevation_tests elevation_test.cpp)
  target_link_libraries(cms_api_elevation_tests PRIVATE cms::api_authority Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_elevation_tests PRIVATE)
  cms_apply_hardening(cms_api_elevation_tests)
  cms_apply_sanitizers(cms_api_elevation_tests)
  catch_discover_tests(cms_api_elevation_tests)

  add_executable(cms_api_bootstrap_tests bootstrap_test.cpp)
  target_link_libraries(cms_api_bootstrap_tests PRIVATE cms::api_authority Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_bootstrap_tests PRIVATE)
  cms_apply_hardening(cms_api_bootstrap_tests)
  cms_apply_sanitizers(cms_api_bootstrap_tests)
  catch_discover_tests(cms_api_bootstrap_tests)

  add_executable(cms_api_settings_view_tests settings_view_test.cpp)
  target_link_libraries(cms_api_settings_view_tests PRIVATE cms::api_authority
                                                           Catch2::Catch2WithMain)
  target_compile_definitions(cms_api_settings_view_tests PRIVATE
    CMS_CATALOGUE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../deploy/catalogue/settings.json")
  cms_apply_warnings(cms_api_settings_view_tests PRIVATE)
  cms_apply_hardening(cms_api_settings_view_tests)
  cms_apply_sanitizers(cms_api_settings_view_tests)
  catch_discover_tests(cms_api_settings_view_tests)

  add_executable(cms_api_settings_write_tests settings_write_test.cpp)
  target_link_libraries(cms_api_settings_write_tests PRIVATE cms::api_authority
                                                             Catch2::Catch2WithMain)
  target_compile_definitions(cms_api_settings_write_tests PRIVATE
    CMS_CATALOGUE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../deploy/catalogue/settings.json")
  cms_apply_warnings(cms_api_settings_write_tests PRIVATE)
  cms_apply_hardening(cms_api_settings_write_tests)
  cms_apply_sanitizers(cms_api_settings_write_tests)
  catch_discover_tests(cms_api_settings_write_tests)

  add_executable(cms_api_settings_plan_tests settings_plan_test.cpp)
  target_link_libraries(cms_api_settings_plan_tests PRIVATE cms::api_authority
                                                            Catch2::Catch2WithMain)
  target_compile_definitions(cms_api_settings_plan_tests PRIVATE
    CMS_CATALOGUE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../deploy/catalogue/settings.json")
  cms_apply_warnings(cms_api_settings_plan_tests PRIVATE)
  cms_apply_hardening(cms_api_settings_plan_tests)
  cms_apply_sanitizers(cms_api_settings_plan_tests)
  catch_discover_tests(cms_api_settings_plan_tests)

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

  add_executable(cms_api_web_tests web_test.cpp)
  target_link_libraries(cms_api_web_tests PRIVATE cms::api_web Catch2::Catch2WithMain)
  target_compile_definitions(cms_api_web_tests PRIVATE
    CMS_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../templates")
  cms_apply_warnings(cms_api_web_tests PRIVATE)
  cms_apply_hardening(cms_api_web_tests)
  cms_apply_sanitizers(cms_api_web_tests)
  catch_discover_tests(cms_api_web_tests)

  add_executable(cms_api_auth_pages_tests auth_pages_test.cpp)
  target_link_libraries(cms_api_auth_pages_tests PRIVATE cms::api_web Catch2::Catch2WithMain)
  target_compile_definitions(cms_api_auth_pages_tests PRIVATE
    CMS_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../templates")
  cms_apply_warnings(cms_api_auth_pages_tests PRIVATE)
  cms_apply_hardening(cms_api_auth_pages_tests)
  cms_apply_sanitizers(cms_api_auth_pages_tests)
  catch_discover_tests(cms_api_auth_pages_tests)

  add_executable(cms_api_verdict_view_tests verdict_view_test.cpp)
  target_link_libraries(cms_api_verdict_view_tests PRIVATE cms::api_web Catch2::Catch2WithMain)
  cms_apply_warnings(cms_api_verdict_view_tests PRIVATE)
  cms_apply_hardening(cms_api_verdict_view_tests)
  cms_apply_sanitizers(cms_api_verdict_view_tests)
  catch_discover_tests(cms_api_verdict_view_tests)
endif()

add_test(
  NAME api_host_config
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/host_config_test.sh" "$<TARGET_FILE:cms_api>")

add_test(
  NAME api_settings_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/settings_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_settings_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK e2e)

add_test(
  NAME api_object_readers_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/object_readers_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_object_readers_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)

add_test(
  NAME api_task_form_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/task_form_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_task_form_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240)

add_test(
  NAME api_submit_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/submit_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_submit_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK e2e)

add_test(
  NAME api_task_route_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/task_route_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_task_route_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK e2e)

add_test(
  NAME api_auth_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/auth_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_auth_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK e2e)

add_test(
  NAME api_task_list_route_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/task_list_route_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_task_list_route_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK e2e)

add_test(
  NAME api_progress_route_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/progress_route_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_progress_route_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RESOURCE_LOCK e2e)

add_test(
  NAME api_settings_route_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/settings_route_test.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_settings_route_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK e2e)

add_test(
  NAME api_model_admin_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/model_admin_e2e.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_model_admin_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK e2e)

add_test(
  NAME api_render_e2e
  COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/check-render.sh" "$<TARGET_FILE:cms_api>")
set_tests_properties(api_render_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RESOURCE_LOCK e2e)
