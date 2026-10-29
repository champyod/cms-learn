# The core owns policy types and nothing else: no HTTP framework, no database driver, no logging
# backend. Keeping it dependency-free is what lets every other component depend on it without
# inheriting a transport, and lets a clean checkout compile it with nothing installed.
add_library(cms_core INTERFACE)
add_library(cms::core ALIAS cms_core)

target_include_directories(cms_core INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")

target_compile_features(cms_core INTERFACE cxx_std_23)
cms_apply_warnings(cms_core INTERFACE)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
