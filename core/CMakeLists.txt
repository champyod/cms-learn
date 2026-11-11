# The core owns policy types and one client that speaks to the object store. Everything else stays out:
# no HTTP framework, no database driver, no logging backend, so a component that only wants the error and
# capability vocabulary links the header-only library and nothing else.
add_library(cms_core INTERFACE)
add_library(cms::core ALIAS cms_core)

# The bytes of a task live in object storage and the row holds only the key (ADR 0023). One client reads
# them; the api, the seeder and the gate all call this one, and nothing else in the tree may speak S3.
find_package(AWSSDK CONFIG REQUIRED COMPONENTS s3)
add_library(cms_object_store STATIC src/object_store.cpp)
add_library(cms::object_store ALIAS cms_object_store)
target_include_directories(cms_object_store PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(cms_object_store PUBLIC cms::core PRIVATE ${AWSSDK_LINK_LIBRARIES})
target_compile_features(cms_object_store PUBLIC cxx_std_26)
cms_apply_warnings(cms_object_store PUBLIC)

# The gate's handle on the client: one object in, its bytes out, with the reason on stderr otherwise.
add_executable(cms_object_get src/object_get.cpp)
target_link_libraries(cms_object_get PRIVATE cms::object_store)
cms_apply_warnings(cms_object_get PRIVATE)

target_include_directories(cms_core INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")

target_compile_features(cms_core INTERFACE cxx_std_26)
cms_apply_warnings(cms_core INTERFACE)

if(CMS_BUILD_TESTS)
  add_subdirectory(tests)
endif()
