find_package(Catch2 3 CONFIG QUIET)
if(Catch2_FOUND)
  # The judge execs a checker by path, and git keeps every script in this repo non-executable, so the stub
  # fixtures are staged into the build tree with the mode that exec contract needs. The sources stay as the
  # repository tracks them.
  set(cms_stub_dir "${CMAKE_CURRENT_BINARY_DIR}/stubs")
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/stubs/" DESTINATION "${cms_stub_dir}")
  file(CHMOD_RECURSE "${cms_stub_dir}" PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

  add_executable(cms_judge_verdict_tests verdict_test.cpp)
  target_link_libraries(cms_judge_verdict_tests PRIVATE cms::judge_verdict Catch2::Catch2WithMain)
  # The stub checkers are the falsification material: one says "pass", one says nothing, one says "ok"
  # and then fails. The path is baked in so the test never guesses where they live.
  target_compile_definitions(cms_judge_verdict_tests PRIVATE
    CMS_CHECKER_DIR="${cms_stub_dir}")
  cms_apply_warnings(cms_judge_verdict_tests PRIVATE)
  cms_apply_hardening(cms_judge_verdict_tests)
  cms_apply_sanitizers(cms_judge_verdict_tests)

  include(Catch)
  catch_discover_tests(cms_judge_verdict_tests)

  add_executable(cms_judge_run_tests run_test.cpp)
  target_link_libraries(cms_judge_run_tests PRIVATE cms::judge_verdict Catch2::Catch2WithMain)
  cms_apply_warnings(cms_judge_run_tests PRIVATE)
  cms_apply_hardening(cms_judge_run_tests)
  cms_apply_sanitizers(cms_judge_run_tests)
  catch_discover_tests(cms_judge_run_tests)

  add_executable(cms_judge_supervisor_tests supervisor_test.cpp)
  target_link_libraries(cms_judge_supervisor_tests PRIVATE cms::judge_verdict
                                                         Catch2::Catch2WithMain)
  target_compile_definitions(cms_judge_supervisor_tests PRIVATE
    CMS_STUB_DIR="${cms_stub_dir}")
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
