#include <cstring>
#include "TestHarness.h"

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;
    for (const auto& t : th::Registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        ++run;
        const int before = th::FailureCount();
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        if (th::FailureCount() != before) {
            ++failedTests;
            std::printf("[ FAIL ] %s (%s)\n", t.name, t.file);
        }
    }
    std::printf("\n%d test(s) run, %d failed, %d failed check(s)\n", run, failedTests, th::FailureCount());
    return failedTests == 0 ? 0 : 1;
}
