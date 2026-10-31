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

  # The vendored testlib has to compile and agree with the parser, or the checker contract is a claim
  # rather than a measurement.
  add_test(
    NAME judge_token_checker
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/token_checker_test.sh"
            "$<TARGET_FILE:cms_checker_token>")
endif()
