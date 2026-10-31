# Overlay of the registry port, pinned to the release that carries the input bounds fixes.
#
# Releases 5.0.6 and 5.0.7 stop three reads that ran past the end of a buffer on truncated or
# unpadded input, which is exactly the shape a request body can take, and 5.0.8 bumps the soversion
# those fixes belong to. 5.1.0 rejects integers wider than 64 bits instead of truncating them and
# accepts exponents of more than 19 digits in get_double. The portfile is otherwise the registry's.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO simdjson/simdjson
    REF "v${VERSION}"
    HEAD_REF master
    SHA512 e166657fdb4a80796ff7991c298aeaab0296dc2a4f8bf3a95a8bde2dbf0eda9cf5e967a6d3bfd140fde4458ba8bf2690dd089edefa82b9033b1da325a1173915
)

vcpkg_check_features(
    OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        exceptions SIMDJSON_EXCEPTIONS
        threads    SIMDJSON_ENABLE_THREADS
    INVERTED_FEATURES
        deprecated SIMDJSON_DISABLE_DEPRECATED_API
        utf8-validation SIMDJSON_SKIPUTF8VALIDATION
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSIMDJSON_DEVELOPER_MODE=OFF
        -DSIMDJSON_SANITIZE_UNDEFINED=OFF
        -DSIMDJSON_SANITIZE=OFF
        -DSIMDJSON_SANITIZE_THREADS=OFF
        -DSIMDJSON_DEVELOPMENT_CHECKS=OFF
        -DSIMDJSON_VERBOSE_LOGGING=OFF
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()

if(VCPKG_TARGET_IS_WINDOWS AND VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    set(SIMDJSON_HEADER "${CURRENT_PACKAGES_DIR}/include/simdjson.h")
    file(READ "${SIMDJSON_HEADER}" SIMDJSON_HEADER_CONTENTS)
    file(WRITE "${SIMDJSON_HEADER}"
        "#ifndef SIMDJSON_USING_WINDOWS_DYNAMIC_LIBRARY\n"
        "#define SIMDJSON_USING_WINDOWS_DYNAMIC_LIBRARY 1\n"
        "#endif\n"
        "${SIMDJSON_HEADER_CONTENTS}"
    )
endif()

vcpkg_copy_pdbs()

vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/${PORT}")

vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/LICENSE"
        "${SOURCE_PATH}/LICENSE-MIT"
        "${SOURCE_PATH}/include/simdjson/nonstd/string_view.hpp"
        "${SOURCE_PATH}/include/simdjson/internal/instruction_set.h"
)
