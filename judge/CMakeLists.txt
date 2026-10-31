# The judge is its own component because it is the only part of the system that runs untrusted code: it
# must build and be gated without the HTTP service, and it must never link back into it.

add_library(cms_judge_verdict STATIC src/verdict.cpp)
add_library(cms::judge_verdict ALIAS cms_judge_verdict)

target_include_directories(cms_judge_verdict PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(cms_judge_verdict PUBLIC cms::core)
cms_apply_warnings(cms_judge_verdict PUBLIC)
cms_apply_hardening(cms_judge_verdict)
cms_apply_sanitizers(cms_judge_verdict)

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
