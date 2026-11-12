# The generation worker: its own process, its own database role, and the deployer's model configuration. The api
# never links this, so the service cannot hold a model key even by accident (ADR 0028).
add_library(cms_generate_model STATIC src/config.cpp src/recorded.cpp)
add_library(cms::generate_model ALIAS cms_generate_model)
target_include_directories(cms_generate_model PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(cms_generate_model PUBLIC cms::core)
target_compile_features(cms_generate_model PUBLIC cxx_std_26)
cms_apply_warnings(cms_generate_model PUBLIC)

add_executable(cms_generate src/main.cpp)
target_link_libraries(cms_generate PRIVATE cms::generate_model)
cms_apply_warnings(cms_generate PRIVATE)
cms_apply_hardening(cms_generate)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
