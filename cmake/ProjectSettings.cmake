# Compiler settings shared by every target in the workspace.
#
# Warnings are applied as a set rather than one at a time because each member names a defect class
# this codebase is not allowed to ship; the set is per-target so a dependency can opt out. Hardening
# is what a distribution build enables, applied where it costs nothing and removes an exploit
# primitive rather than a fallback in the source.

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CMS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
set(CMS_SANITIZE "" CACHE STRING "Sanitizers for development builds, e.g. address;undefined")

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
