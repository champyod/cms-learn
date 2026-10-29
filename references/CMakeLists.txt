# One executable and one ctest case per reference solution, discovered from the content tree so adding a
# task needs no build-file edit.
#
# CMake caches the glob, so a developer who adds a task re-configures once (`bun run check:cpp` always
# configures from scratch, so CI can never see a stale list).

set(content "${CMAKE_CURRENT_SOURCE_DIR}/../content/tasks")
file(GLOB task_paths LIST_DIRECTORIES true "${content}/*")

set(executables 0)
set(cases 0)

foreach(task_path ${task_paths})
  file(GLOB solutions "${task_path}/solutions/*.cpp")
  file(GLOB inputs "${task_path}/examples/*.in")
  get_filename_component(task_name "${task_path}" NAME)

  foreach(source ${solutions})
    get_filename_component(source_name "${source}" NAME_WE)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" slug "${task_name}_${source_name}")
    set(target "ref_${slug}")

    add_executable(${target} "${source}")
    cms_learn_apply_warnings(${target})
    math(EXPR executables "${executables}+1")

    foreach(input ${inputs})
      get_filename_component(example "${input}" NAME_WE)
      string(REGEX REPLACE "\\.in$" ".out" expected "${input}")

      add_test(
        NAME "${target}.${example}"
        COMMAND ${CMAKE_COMMAND}
          "-DEXE=$<TARGET_FILE:${target}>"
          "-DINPUT=${input}"
          "-DEXPECTED=${expected}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CompareExample.cmake"
      )
      math(EXPR cases "${cases}+1")

      if(CMS_LEARN_TEST_ENVIRONMENT)
        set_tests_properties("${target}.${example}"
          PROPERTIES ENVIRONMENT "${CMS_LEARN_TEST_ENVIRONMENT}")
      endif()
    endforeach()
  endforeach()
endforeach()

message(STATUS "cms-learn: ${executables} reference executable(s), ${cases} ctest case(s)")

if(executables EQUAL 0)
  # An empty build is not a pass.
  message(FATAL_ERROR "no reference solutions found under ${content}")
endif()
