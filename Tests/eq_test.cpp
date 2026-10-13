#include "eq_tests.h"
int main() { int f = eqt::runAll(); std::printf(f ? "\n%d EQ test(s) FAILED\n" : "\nAll EQ tests passed\n", f); return f; }
