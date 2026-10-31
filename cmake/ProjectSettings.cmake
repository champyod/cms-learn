# Compiler settings shared by every target in the workspace.
#
# Warnings are applied as a set rather than one at a time because each member names a defect class
# this codebase is not allowed to ship; the set is per-target so a dependency can opt out. Hardening
# is what a distribution build enables, applied where it costs nothing and removes an exploit
# primitive rather than a fallback in the source.

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CMS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(CMS_LINT_ONLY "Configure for static analysis only; this configuration ships nothing" OFF)
set(CMS_SANITIZE "" CACHE STRING "Sanitizers for development builds, e.g. address;undefined")

# GCC 16 is the first release that implements the C++26 the targets are written in; 14 and 15 accept
# -std=c++26 and then compile a smaller language (no contracts, reflection or expansion statements), so a
# build that succeeds under one of them is not the build this project gates. A lint-only configure is
# exempt because it produces no artefact — the analyser reads the sources and nothing it configures is
# shipped — which is not the same claim as admitting another compiler family at the same numeric floor.
set(CMS_MIN_GCC_MAJOR 16 CACHE STRING "Minimum GCC major version this project compiles under")

if(NOT CMS_LINT_ONLY)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
     OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS CMS_MIN_GCC_MAJOR)
    message(FATAL_ERROR
      "configure refused: this project compiles under GNU g++ ${CMS_MIN_GCC_MAJOR} or newer; found compiler "
      "ID '${CMAKE_CXX_COMPILER_ID}', full version '${CMAKE_CXX_COMPILER_VERSION}', required minimum "
      "'${CMS_MIN_GCC_MAJOR}', compiler '${CMAKE_CXX_COMPILER}'.")
  endif()
endif()

function(cms_apply_warnings target scope)
  if(MSVC)
    target_compile_options(${target} ${scope} /W4 /permissive- /Zc:__cplusplus)
    if(CMS_WARNINGS_AS_ERRORS)
      target_compile_options(${target} ${scope} /WX)
    endif()
    return()
  endif()
  target_compile_options(${target} ${scope}
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wcast-qual
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wnull-dereference
    -Woverloaded-virtual
    -Wundef
    -Wunused)
  # Turns the standard library's unchecked paths (vector::operator[], string::front) into aborts rather
  # than silent reads past the end — the difference between an ASan report and a wrong answer.
  target_compile_definitions(${target} ${scope} _GLIBCXX_ASSERTIONS)
  if(CMS_WARNINGS_AS_ERRORS)
    target_compile_options(${target} ${scope} -Werror)
  endif()
endfunction()

function(cms_apply_hardening target)
  if(MSVC)
    return()
  endif()
  target_compile_options(${target} PRIVATE -fstack-protector-strong -fno-omit-frame-pointer)
  target_compile_definitions(${target} PRIVATE
    $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:_FORTIFY_SOURCE=3>)
  target_link_options(${target} PRIVATE -Wl,-z,relro -Wl,-z,now -Wl,--as-needed)
endfunction()

function(cms_apply_sanitizers target)
  if(MSVC OR CMS_SANITIZE STREQUAL "")
    return()
  endif()
  target_compile_options(${target} PRIVATE -fsanitize=${CMS_SANITIZE} -fno-sanitize-recover=all)
  target_link_options(${target} PRIVATE -fsanitize=${CMS_SANITIZE})
endfunction()
