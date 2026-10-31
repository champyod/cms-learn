# The warning set, declared once and applied to every target.
#
# `-Wconversion` and `-Wsign-conversion` are deliberately absent: judge code crosses between `int` and
# `size_t` at every container boundary by definition, so those two flags turn normal code into noise
# that people then suppress — and a suppressed warning set is worse than a smaller honest one.
function(cms_learn_apply_warnings target)
  target_compile_features(${target} PRIVATE cxx_std_26)
  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wformat=2
    -Wcast-qual
    -Wdouble-promotion
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wundef
  )
  # Turns the standard library's unchecked paths (vector::operator[], string::front) into aborts rather
  # than silent reads past the end — the difference between an ASan report and a wrong answer.
  target_compile_definitions(${target} PRIVATE _GLIBCXX_ASSERTIONS)
endfunction()
