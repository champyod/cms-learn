# The api is the only component that serves HTTP, so it is the only one that discovers Drogon. The
# configuration loader is its own library so the tests can link it without pulling a server in.
find_package(Drogon CONFIG REQUIRED)
include(DrogonCompat)
cms_learn_align_jsoncpp()
find_package(PostgreSQL REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)

# Identity generation is its own library so the password/uuid tests can link it without the service.
add_library(cms_api_identity STATIC src/uuid.cpp)
add_library(cms::api_identity ALIAS cms_api_identity)

target_include_directories(cms_api_identity PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

target_link_libraries(cms_api_identity PUBLIC unofficial-sodium::sodium)
cms_apply_warnings(cms_api_identity PUBLIC)
cms_apply_hardening(cms_api_identity)
cms_apply_sanitizers(cms_api_identity)

add_library(cms_api_config STATIC src/host_config.cpp)
add_library(cms::api_config ALIAS cms_api_config)

target_include_directories(cms_api_config PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

target_link_libraries(cms_api_config PUBLIC cms::core)
cms_apply_warnings(cms_api_config PUBLIC)
cms_apply_hardening(cms_api_config)
cms_apply_sanitizers(cms_api_config)

add_executable(cms_api src/main.cpp src/settings_source.cpp src/settings_watcher.cpp
                       src/admin_socket.cpp)
target_link_libraries(cms_api PRIVATE cms::api_config cms::api_identity cms::db_drogon Drogon::Drogon
                                       PostgreSQL::PostgreSQL)
cms_apply_warnings(cms_api PRIVATE)
cms_apply_hardening(cms_api)
cms_apply_sanitizers(cms_api)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
