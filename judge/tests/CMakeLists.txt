find_package(Catch2 3 CONFIG QUIET)
if(Catch2_FOUND)
  add_executable(cms_judge_verdict_tests verdict_test.cpp)
  target_link_libraries(cms_judge_verdict_tests PRIVATE cms::judge_verdict Catch2::Catch2WithMain)
  # The stub checkers are the falsification material: one says "pass", one says nothing, one says "ok"
  # and then fails. The path is baked in so the test never guesses where they live.
  target_compile_definitions(cms_judge_verdict_tests PRIVATE
    CMS_CHECKER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
  cms_apply_warnings(cms_judge_verdict_tests PRIVATE)
  cms_apply_hardening(cms_judge_verdict_tests)
  cms_apply_sanitizers(cms_judge_verdict_tests)

  include(Catch)
  catch_discover_tests(cms_judge_verdict_tests)

  add_executable(cms_judge_supervisor_tests supervisor_test.cpp)
  target_link_libraries(cms_judge_supervisor_tests PRIVATE cms::judge_verdict
                                                         Catch2::Catch2WithMain)
  target_compile_definitions(cms_judge_supervisor_tests PRIVATE
    CMS_STUB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stubs")
  cms_apply_warnings(cms_judge_supervisor_tests PRIVATE)
  cms_apply_hardening(cms_judge_supervisor_tests)
  cms_apply_sanitizers(cms_judge_supervisor_tests)
  catch_discover_tests(cms_judge_supervisor_tests)

  # A claim is a database operation, so its test exists only where the connector does.
  if(TARGET cms::judge_worker AND TARGET cms::db_drogon)
    add_executable(cms_judge_worker_tests worker_test.cpp)
    target_link_libraries(cms_judge_worker_tests PRIVATE cms::judge_worker cms::db_drogon
                                                           Catch2::Catch2WithMain)
    cms_apply_warnings(cms_judge_worker_tests PRIVATE)
    cms_apply_hardening(cms_judge_worker_tests)
    cms_apply_sanitizers(cms_judge_worker_tests)
    catch_discover_tests(cms_judge_worker_tests)
  endif()

  # The vendored testlib has to compile and agree with the parser, or the checker contract is a claim
  # rather than a measurement.
  add_test(
    NAME judge_token_checker
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/token_checker_test.sh"
            "$<TARGET_FILE:cms_checker_token>")
endif()
