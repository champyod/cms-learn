find_package(Catch2 3 CONFIG QUIET)
if(Catch2_FOUND)
  add_executable(cms_generate_model_tests model_test.cpp)
  target_link_libraries(cms_generate_model_tests PRIVATE cms::generate_model Catch2::Catch2WithMain)
  cms_apply_warnings(cms_generate_model_tests PRIVATE)
  cms_apply_hardening(cms_generate_model_tests)
  cms_apply_sanitizers(cms_generate_model_tests)
  catch_discover_tests(cms_generate_model_tests)

  add_executable(cms_generate_steps_tests planner_test.cpp judge_test.cpp candidates_test.cpp
                                     artefact_test.cpp claim_test.cpp framing_test.cpp
                                     specification_test.cpp)
  target_link_libraries(cms_generate_steps_tests PRIVATE cms::generate_model cms::object_store
                                                   Catch2::Catch2WithMain)
  cms_apply_warnings(cms_generate_steps_tests PRIVATE)
  cms_apply_hardening(cms_generate_steps_tests)
  cms_apply_sanitizers(cms_generate_steps_tests)
  catch_discover_tests(cms_generate_steps_tests)
endif()
