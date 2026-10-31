find_package(Catch2 3 CONFIG QUIET)
if(Catch2_FOUND)
  add_executable(cms_bank_tests bank_test.cpp)
  target_link_libraries(cms_bank_tests PRIVATE cms::bank Catch2::Catch2WithMain)
  # The test reads the bank that ships with the image, so the build records where that tree is.
  target_compile_definitions(cms_bank_tests PRIVATE
    CMS_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../content")
  cms_apply_warnings(cms_bank_tests PRIVATE)
  cms_apply_hardening(cms_bank_tests)
  cms_apply_sanitizers(cms_bank_tests)

  add_executable(cms_curriculum_tests curriculum_test.cpp)
  target_link_libraries(cms_curriculum_tests PRIVATE cms::bank Catch2::Catch2WithMain)
  target_compile_definitions(cms_curriculum_tests PRIVATE
    CMS_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../content")
  cms_apply_warnings(cms_curriculum_tests PRIVATE)
  cms_apply_hardening(cms_curriculum_tests)
  cms_apply_sanitizers(cms_curriculum_tests)
  catch_discover_tests(cms_curriculum_tests)

  add_executable(cms_serve_tests serve_test.cpp)
  target_link_libraries(cms_serve_tests PRIVATE cms::bank Catch2::Catch2WithMain)
  target_compile_definitions(cms_serve_tests PRIVATE
    CMS_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../content")
  cms_apply_warnings(cms_serve_tests PRIVATE)
  cms_apply_hardening(cms_serve_tests)
  cms_apply_sanitizers(cms_serve_tests)
  catch_discover_tests(cms_serve_tests)

  include(Catch)
  catch_discover_tests(cms_bank_tests)
endif()
