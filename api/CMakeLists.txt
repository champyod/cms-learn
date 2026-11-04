# The api is the only component that serves HTTP, so it is the only one that discovers Drogon. The
# configuration loader is its own library so the tests can link it without pulling a server in.
find_package(Drogon CONFIG REQUIRED)
include(DrogonCompat)
cms_learn_align_jsoncpp()
find_package(PostgreSQL REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(inja CONFIG REQUIRED)

# Mail is its own library: it is the only part of the api that talks to the network on its own, and it
# is disabled by default, so it must be testable without a server.
add_library(cms_api_mail STATIC src/settings_defaults.cpp src/mailer.cpp)
add_library(cms::api_mail ALIAS cms_api_mail)
target_include_directories(cms_api_mail PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(cms_api_mail PUBLIC cms::core CURL::libcurl)
cms_apply_warnings(cms_api_mail PUBLIC)
cms_apply_hardening(cms_api_mail)
cms_apply_sanitizers(cms_api_mail)

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

# The web layer is its own library so a page can be rendered and an asset path resolved without a
# server, a database or a listening socket.
add_library(cms_api_web STATIC src/web.cpp)
add_library(cms::api_web ALIAS cms_api_web)

target_include_directories(cms_api_web PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

target_link_libraries(cms_api_web PUBLIC cms::bank pantor::inja)
cms_apply_warnings(cms_api_web PUBLIC)
cms_apply_hardening(cms_api_web)
cms_apply_sanitizers(cms_api_web)

# Authority is its own library so the account state machine, the capability check, elevation and the
# control protocol are testable without Drogon's server or a file descriptor.
add_library(cms_api_authority STATIC src/accounts.cpp src/audit.cpp src/authorize.cpp
                                      src/elevation.cpp src/admin_socket.cpp
                                      src/admin_protocol.cpp src/submissions.cpp
                                      src/settings_view.cpp src/settings_write.cpp
                                      src/settings_plan.cpp src/settings_route.cpp
                                      src/task_view.cpp)
add_library(cms::api_authority ALIAS cms_api_authority)
target_include_directories(cms_api_authority PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(cms_api_authority PUBLIC cms::api_identity cms::api_mail cms::db_drogon
                                             cms::bank simdjson::simdjson)
cms_apply_warnings(cms_api_authority PUBLIC)
cms_apply_hardening(cms_api_authority)
cms_apply_sanitizers(cms_api_authority)

add_executable(cms_api src/main.cpp src/page_routes.cpp src/settings_source.cpp
                       src/settings_watcher.cpp)

# The catalogue is a build input, not operator configuration: a key this binary was not compiled against is
# not a setting it can honour, so the build names the file it was built from rather than letting a runtime
# value offer controls nothing honours. A missing file stops the service at startup, naming the path.
target_compile_definitions(cms_api PRIVATE
  CMS_CATALOGUE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../deploy/catalogue/settings.json")
target_link_libraries(cms_api PRIVATE cms::api_authority cms::api_config cms::api_identity
                                       cms::api_mail cms::api_web cms::db_drogon Drogon::Drogon
                                       PostgreSQL::PostgreSQL)
cms_apply_warnings(cms_api PRIVATE)
cms_apply_hardening(cms_api)
cms_apply_sanitizers(cms_api)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
