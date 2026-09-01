#pragma once

// The scaffolding every headless suite was writing out for itself.
//
// Each suite is its own executable that counts failing checks and returns a
// non-zero exit code, which is all CTest needs. That is a good shape and it has
// not changed. What had happened is that the same dozen lines were copied into
// every file, and the copies drifted: the check function took `ok` in some
// files, `condition` or `value` in others, and roughly half printed the name of
// the running test in a failure while the rest printed only a line number.
//
// Both behaviours survive here, chosen by whether a suite calls TEST(). A file
// that never names its tests prints exactly what it printed before, so adopting
// this header changes no output.
//
// Deliberately small. It is not a test framework: no fixtures, no registration,
// no assertion vocabulary beyond CHECK. Each suite still owns its own main() and
// decides what to run and what to report, because that is the part that differs
// between suites for real reasons.

#include <iostream>

// Counted across the whole run, and read by each suite's main() to decide its
// exit code. Global rather than namespaced because that is where the per-file
// copies sat, and because a suite's main() reads them unqualified.
inline int failures = 0;
inline int checks = 0;

// The test currently running, when a suite bothers to say. Empty means a suite
// that does not use TEST(), and failures from it are reported without a name -
// which is what those files did before this header existed.
inline const char* currentTest = "";

inline void checkImpl(bool ok, const char* expression, int line)
{
    ++checks;
    if (ok) {
        return;
    }
    ++failures;
    std::cerr << "FAIL ";
    if (currentTest[0] != '\0') {
        std::cerr << '[' << currentTest << "] ";
    }
    std::cerr << "line " << line << ": " << expression << '\n';
}

// Stringizes the expression, so a failure reports what was asked rather than a
// hand-written label that can fall out of step with the code beside it.
#define CHECK(expression) checkImpl((expression), #expression, __LINE__)

// Names the section that follows. Optional: a suite that does not call it still
// reports failures, just without a name.
#define TEST(name) currentTest = name
