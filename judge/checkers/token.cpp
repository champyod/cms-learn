// A token checker: the answer is correct when both files hold the same sequence of whitespace-separated
// tokens. Used by the judge's own tests as the proof that the vendored testlib compiles and that the
// judge reads the verdict testlib writes.

#include "testlib.h"

int main(int argc, char* argv[]) {
  registerTestlibCmd(argc, argv);

  int position = 0;
  while (!ans.seekEof() && !ouf.seekEof()) {
    ++position;
    const auto expected = ans.readToken();
    const auto actual = ouf.readToken();
    if (expected != actual) {
      quitf(_wa, "token %d differs - expected '%s', found '%s'", position,
            compress(expected).c_str(), compress(actual).c_str());
    }
  }

  if (!ans.seekEof()) {
    quitf(_wa, "the answer was not consumed: trailing tokens are missing");
  }
  if (!ouf.seekEof()) {
    quitf(_pe, "extra tokens after the answer");
  }
  quitf(_ok, "tokens match");
}
