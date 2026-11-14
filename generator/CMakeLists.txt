# The generation worker: its own process, its own database role, and the deployer's model configuration. The api
# never links this, so the service cannot hold a model key even by accident (ADR 0028).
find_package(simdjson CONFIG REQUIRED)
find_package(Drogon CONFIG REQUIRED)
include(DrogonCompat)
cms_learn_align_jsoncpp()

add_library(cms_generate_model STATIC src/artefact.cpp src/brief.cpp src/candidate.cpp src/candidates.cpp
                                   src/config.cpp src/judge.cpp src/planner.cpp src/recorded.cpp)
add_library(cms::generate_model ALIAS cms_generate_model)
target_include_directories(cms_generate_model PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cms_generate_model PUBLIC cms::core cms::db cms::db_drogon cms::object_store
                                     simdjson::simdjson)
target_compile_features(cms_generate_model PUBLIC cxx_std_26)
cms_apply_warnings(cms_generate_model PUBLIC)
cms_apply_hardening(cms_generate_model)

add_executable(cms_generate src/main.cpp)
target_link_libraries(cms_generate PRIVATE cms::generate_model)
cms_apply_warnings(cms_generate PRIVATE)
cms_apply_hardening(cms_generate)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
