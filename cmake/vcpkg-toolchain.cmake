# Named by the configure presets as their toolchain, so that every target resolves third-party packages
# from one place.
#
# The core and the reference solutions carry no third-party dependency, and a clean checkout has to keep
# configuring without vcpkg. The api and the judge do depend on it, so their absence is fatal rather than
# a later "package not found".

set(_cms_vcpkg_root "$ENV{VCPKG_ROOT}")
set(_cms_vcpkg_toolchain "${_cms_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")

# CMake re-enters a toolchain file during its own configure, and vcpkg's script must be included once.
if(DEFINED _CMS_VCPKG_TOOLCHAIN_ENTERED)
  return()
endif()
set(_CMS_VCPKG_TOOLCHAIN_ENTERED TRUE)

if(_cms_vcpkg_root STREQUAL "")
  if(CMS_WITH_API OR CMS_WITH_JUDGE)
    message(FATAL_ERROR
      "VCPKG_ROOT is not set, but CMS_WITH_API or CMS_WITH_JUDGE needs the packages it provides. "
      "Point VCPKG_ROOT at a vcpkg checkout.")
  endif()
  message(STATUS "cms-learn: VCPKG_ROOT is not set; no third-party package is resolved")
  return()
endif()

if(NOT EXISTS "${_cms_vcpkg_toolchain}")
  message(FATAL_ERROR
    "VCPKG_ROOT is '${_cms_vcpkg_root}', which has no scripts/buildsystems/vcpkg.cmake. "
    "It must name the vcpkg checkout, not its bin directory.")
endif()

include("${_cms_vcpkg_toolchain}")
