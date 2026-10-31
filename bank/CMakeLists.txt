# The bank is data and the seeder is the only writer, so it is its own library plus one executable: the
# gate runs the executable against a scratch cluster, and a test can call seed() directly.

find_package(simdjson CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(Drogon CONFIG REQUIRED)
include(DrogonCompat)
cms_learn_align_jsoncpp()

add_library(cms_bank STATIC src/bank.cpp src/curriculum.cpp src/task_files.cpp
                          src/task_manifest.cpp src/track_stage.cpp src/tracks.cpp src/seed.cpp
                          src/seed_curriculum.cpp src/next_task.cpp)
add_library(cms::bank ALIAS cms_bank)

target_include_directories(cms_bank PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(cms_bank PUBLIC cms::core cms::db cms::db_drogon
                                     simdjson::simdjson unofficial-sodium::sodium)
cms_apply_warnings(cms_bank PUBLIC)
cms_apply_hardening(cms_bank)
cms_apply_sanitizers(cms_bank)

add_executable(cms_seed_content src/main.cpp)
target_link_libraries(cms_seed_content PRIVATE cms::bank)
cms_apply_warnings(cms_seed_content PRIVATE)
cms_apply_hardening(cms_seed_content)
cms_apply_sanitizers(cms_seed_content)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
