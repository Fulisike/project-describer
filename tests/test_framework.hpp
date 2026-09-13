#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {

struct Case {
    std::string name;
    std::function<void()> run;
};

inline std::vector<Case>& Cases() {
    static std::vector<Case> cases;
    return cases;
}

inline int& Failures() {
    static int failures = 0;
    return failures;
}

struct Register {
    Register(const char* name, std::function<void()> run) {
        Cases().push_back({name, std::move(run)});
    }
};

inline void Expect(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        ++Failures();
        std::cerr << file << ':' << line << ": expectation failed: " << expression << '\n';
    }
}

}  // namespace test

#define TEST(name) \
    void name(); \
    const test::Register register_##name(#name, name); \
    void name()

#define EXPECT_TRUE(expression) test::Expect(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define EXPECT_EQ(left, right) test::Expect((left) == (right), #left " == " #right, __FILE__, __LINE__)
