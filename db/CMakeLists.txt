# A clean checkout configures without vcpkg, so an absent driver skips this component rather than
# failing the build; the manifest dependency is what supplies it in every build that has one.
find_package(Sqlpp23 CONFIG QUIET)
if(NOT Sqlpp23_FOUND)
  message(STATUS "sqlpp23 not found; the database layer is skipped (install the 'sqlpp23' manifest dependency)")
  return()
endif()

# The table types stay an interface target with no transport behind them, so a consumer that only
# names a column never inherits an HTTP framework.
add_library(cms_db INTERFACE)
add_library(cms::db ALIAS cms_db)

target_include_directories(cms_db INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")

target_link_libraries(cms_db INTERFACE sqlpp23::core)
target_compile_features(cms_db INTERFACE cxx_std_26)
cms_apply_warnings(cms_db INTERFACE)

# The connector is its own target so the table types stay free of the transport: only a component
# that reaches the server links Drogon.
find_package(Drogon CONFIG QUIET)
if(Drogon_FOUND)
  include(DrogonCompat)
  cms_learn_align_jsoncpp()

  add_library(cms_db_drogon STATIC src/connection.cpp)
  add_library(cms::db_drogon ALIAS cms_db_drogon)

  target_link_libraries(cms_db_drogon PUBLIC cms::db Drogon::Drogon)
  target_compile_features(cms_db_drogon PUBLIC cxx_std_26)
  cms_apply_warnings(cms_db_drogon PRIVATE)
  cms_apply_hardening(cms_db_drogon)
  cms_apply_sanitizers(cms_db_drogon)
else()
  message(STATUS "Drogon not found; the sqlpp23 connector is skipped (install the 'drogon' manifest dependency)")
endif()

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
