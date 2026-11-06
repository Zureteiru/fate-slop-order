// Runs the vendored switch-vnpop dictionary tests against vnreader's build.
#include "vnpop_ledger.h"

#include <cstdio>

void run_dict_tests(void);
void run_lookup_tests(void);

int main() {
    vnpop_ledger_init();
    run_dict_tests();
    run_lookup_tests();
    std::printf("[PASS] dict_tests\n");
    return 0;
}
