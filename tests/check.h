// Minimal test harness: TEST(name) { CHECK(...); }  — registered automatically, run by core_main.cpp.
#pragma once

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

template <class T>
std::string show(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}
inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(const char* v) { return show(std::string(v)); }
inline std::string show(bool v) { return v ? "true" : "false"; }

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, msg.c_str());
}

}  // namespace check

#define TEST(name)                                                   \
    static void name();                                              \
    static const check::Registrar name##_registrar(#name, &name);    \
    static void name()

#define CHECK(cond)                                                  \
    do {                                                             \
        if (!(cond)) check::fail(__FILE__, __LINE__, #cond);         \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        const auto& va_ = (a);                                                                      \
        const auto& vb_ = (b);                                                                      \
        if (!(va_ == vb_))                                                                          \
            check::fail(__FILE__, __LINE__,                                                         \
                        std::string(#a " == " #b "  (") + check::show(va_) + " vs " + check::show(vb_) + ")"); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                       \
    do {                                                                                            \
        const double va_ = (a), vb_ = (b);                                                          \
        if (!(std::fabs(va_ - vb_) <= (eps)))                                                       \
            check::fail(__FILE__, __LINE__,                                                         \
                        std::string(#a " ~= " #b "  (") + check::show(va_) + " vs " + check::show(vb_) + ")"); \
    } while (0)

// Stops the current test when a precondition fails (avoids null dereferences).
#define REQUIRE(cond)                                                \
    do {                                                             \
        if (!(cond)) { check::fail(__FILE__, __LINE__, #cond); return; } \
    } while (0)
