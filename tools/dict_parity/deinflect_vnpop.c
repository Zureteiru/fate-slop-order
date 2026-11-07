/* vnpop's deinflections for each input line, in the same format as deinflect_hoshi.cpp. */
#include <stdio.h>
#include <string.h>

#include "vnpop_deinflect.h"

static VnpopDeinflectCandidate s_out[VNPOP_MAX_DEINFLECT_CANDIDATES];

int main(void) {
    static char line[1024];
    size_t truncated = 0;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        size_t n = vnpop_deinflect(line, s_out, VNPOP_MAX_DEINFLECT_CANDIDATES);
        if (n == VNPOP_MAX_DEINFLECT_CANDIDATES) ++truncated;
        printf("%s\t", line);
        for (size_t i = 0; i < n; ++i)
            printf("%s%s|%u|%s", i ? " ; " : "", s_out[i].term, (unsigned)s_out[i].conds, s_out[i].rule_trace);
        printf("\n");
    }
    fprintf(stderr, "vnpop: %zu inputs hit the candidate cap\n", truncated);
    return 0;
}
