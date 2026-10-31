# The api is the only component that serves HTTP, so it is the only one that discovers Drogon. The
# configuration loader is its own library so the tests can link it without pulling a server in.
find_package(Drogon CONFIG REQUIRED)
include(DrogonCompat)
cms_learn_align_jsoncpp()
find_package(PostgreSQL REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(CURL REQUIRED)

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

# Accounts are their own library so the state machine is testable without Drogon's server.
add_library(cms_api_accounts STATIC src/accounts.cpp src/authorize.cpp)
add_library(cms::api_accounts ALIAS cms_api_accounts)
target_include_directories(cms_api_accounts PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(cms_api_accounts PUBLIC cms::api_identity cms::api_mail cms::db_drogon)
cms_apply_warnings(cms_api_accounts PUBLIC)
cms_apply_hardening(cms_api_accounts)
cms_apply_sanitizers(cms_api_accounts)

add_executable(cms_api src/main.cpp src/settings_source.cpp src/settings_watcher.cpp
                       src/admin_socket.cpp)
target_link_libraries(cms_api PRIVATE cms::api_accounts cms::api_config cms::api_identity
                                       cms::api_mail cms::db_drogon Drogon::Drogon
                                       PostgreSQL::PostgreSQL)
cms_apply_warnings(cms_api PRIVATE)
cms_apply_hardening(cms_api)
cms_apply_sanitizers(cms_api)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
