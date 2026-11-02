# The judge is its own component because it is the only part of the system that runs untrusted code: it
# must build and be gated without the HTTP service, and it must never link back into it.

add_library(cms_judge_verdict STATIC src/verdict.cpp src/manifest.cpp src/supervisor.cpp
                                 src/verify.cpp src/generate.cpp src/promote.cpp
                                 src/child_watch.cpp)
add_library(cms::judge_verdict ALIAS cms_judge_verdict)

target_include_directories(cms_judge_verdict PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
# Verification hashes every case, so the judge needs a digest the same way the bank does.
find_package(unofficial-sodium CONFIG REQUIRED)
target_link_libraries(cms_judge_verdict PUBLIC cms::core unofficial-sodium::sodium)
cms_apply_warnings(cms_judge_verdict PUBLIC)
cms_apply_hardening(cms_judge_verdict)
cms_apply_sanitizers(cms_judge_verdict)

# The claim is the judge's only write to the database, and it is its own target so a build without the
# sqlpp23 headers can still verify tasks: the claim is absent rather than present and unlinked.
if(TARGET cms::db)
  add_library(cms_judge_worker STATIC src/worker.cpp)
  add_library(cms::judge_worker ALIAS cms_judge_worker)
  target_link_libraries(cms_judge_worker PUBLIC cms::judge_verdict cms::db)
  cms_apply_warnings(cms_judge_worker PRIVATE)
  cms_apply_hardening(cms_judge_worker)
  cms_apply_sanitizers(cms_judge_worker)
endif()

# The verifier is the one code path CI and an author both run, so a manifest cannot describe cases that
# were checked by a different rule than the judge applies.
add_executable(cms_verify_task src/verify_main.cpp)
target_link_libraries(cms_verify_task PRIVATE cms::judge_verdict)
cms_apply_warnings(cms_verify_task PRIVATE)
cms_apply_hardening(cms_verify_task)
cms_apply_sanitizers(cms_verify_task)

add_executable(cms_generate_task src/generate_main.cpp)
target_link_libraries(cms_generate_task PRIVATE cms::judge_verdict)
cms_apply_warnings(cms_generate_task PRIVATE)
cms_apply_hardening(cms_generate_task)
cms_apply_sanitizers(cms_generate_task)

add_executable(cms_promote_task src/promote_main.cpp)
target_link_libraries(cms_promote_task PRIVATE cms::judge_verdict)
cms_apply_warnings(cms_promote_task PRIVATE)
cms_apply_hardening(cms_promote_task)
cms_apply_sanitizers(cms_promote_task)

# The checkers are separate executables because that is how they run: one process per case, arguments
# (input, output, answer), verdict on standard error.
add_executable(cms_checker_token checkers/token.cpp)
target_include_directories(cms_checker_token PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
# testlib is a third-party header under a licence this project does not own; its warnings are not ours to
# fix, so it is included as a system header.
target_include_directories(cms_checker_token SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
cms_apply_warnings(cms_checker_token PRIVATE)
cms_apply_hardening(cms_checker_token)
cms_apply_sanitizers(cms_checker_token)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
