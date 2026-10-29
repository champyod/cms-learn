# AddressSanitizer + UndefinedBehaviorSanitizer for every target and every test.
#
# These live in the build, not in the gate: the same flags produce the binaries CI tests and the ones a
# developer runs locally, so "it passed the gate" cannot mean "a different configuration passed".
option(CMS_LEARN_SANITIZE "build with address+undefined sanitizers" ON)

if(CMS_LEARN_SANITIZE)
  add_compile_options(
    -fsanitize=address,undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -g
  )
  add_link_options(-fsanitize=address,undefined)

  # Tests inherit the same strictness: a sanitizer finding aborts instead of printing and letting the
  # process exit 0, which is exactly how a "passing" test hides a use-after-free.
  set(CMS_LEARN_TEST_ENVIRONMENT
    "ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:strict_string_checks=1"
    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
  )
endif()
