# ctest helper: run one reference on one example and compare its output.
#
# Invoked as:
#   cmake -DEXE=<binary> -DINPUT=<n.in> -DEXPECTED=<n.out> -P CompareExample.cmake
#
# Comparison is `string(STRIP)` on both sides rather than a line-by-line normaliser: every example in
# the bank is whitespace-insensitive at the edges only, and a smarter comparer here would quietly accept
# output that a judge with a strict checker would reject.

foreach(required EXE INPUT EXPECTED)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "CompareExample.cmake: -D${required}=... is required")
  endif()
endforeach()

execute_process(
  COMMAND "${EXE}"
  INPUT_FILE "${INPUT}"
  OUTPUT_VARIABLE produced
  ERROR_VARIABLE errors
  RESULT_VARIABLE status
)

if(NOT status EQUAL 0)
  # A sanitizer abort ends up here with a non-zero status; its report is on stderr and must be shown,
  # otherwise the failure reads as a wrong answer instead of a memory error.
  message(FATAL_ERROR "exited with ${status}\n${errors}")
endif()

file(READ "${EXPECTED}" wanted)
string(STRIP "${produced}" produced)
string(STRIP "${wanted}" wanted)

if(NOT produced STREQUAL wanted)
  message(FATAL_ERROR "expected:\n${wanted}\ngot:\n${produced}")
endif()
