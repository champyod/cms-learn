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
                                     pipeline_test.cpp specification_test.cpp solution_test.cpp
                                     statement_test.cpp test_plan_test.cpp certify_test.cpp)
  target_link_libraries(cms_generate_steps_tests PRIVATE cms::generate_model cms::object_store
                                                   Catch2::Catch2WithMain)
  # The vocabulary the pipeline's step 4 reads is the content tree that ships with the image, so the build records it.
  target_compile_definitions(cms_generate_steps_tests PRIVATE
    CMS_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../content")
  cms_apply_warnings(cms_generate_steps_tests PRIVATE)
  cms_apply_hardening(cms_generate_steps_tests)
  cms_apply_sanitizers(cms_generate_steps_tests)
  catch_discover_tests(cms_generate_steps_tests)
endif()
