#include "test_framework.hpp"

int main() {
    for (const auto& test_case : test::Cases()) {
        test_case.run();
    }
    if (test::Failures() == 0) {
        std::cout << test::Cases().size() << " tests passed\n";
        return 0;
    }
    std::cerr << test::Failures() << " expectations failed\n";
    return 1;
}
