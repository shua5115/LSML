#include "lsml.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define LSML_TRY(expr) do { lsml_err_t err__ = (expr); if (err__) { lsml_print_line_info("LSML error: %s at %s:%u\n", lsml_strerr(err__), __FILE__, __LINE__); return err__; } } while(0)
#define LSML_ASSERT(expr) do { if(!(expr)) { lsml_print_line_info("LSML assertion failed: %s at %s:%u\n", #expr, __FILE__, __LINE__); return -1; } } while(0)
static void lsml_print_line_info(const char *fmt, const char *expr, const char *file, unsigned int line) {
    fprintf(stderr, fmt, expr, file, line);
}

static void print_mem_usage(lsml_data_t *data, const char *event) {
    unsigned long long mem = lsml_data_mem_usage(data);
    printf("%llu bytes used after %s\n", mem, event);
}

#define MEM_CAP (16*1024*1024)

int main() {
    char *scratch = (char *) malloc(MEM_CAP);
    if (scratch == NULL) {
        fprintf(stderr, "initial allocation failed");
        return -1;
    }
    lsml_data_t *data = lsml_data_new(scratch, MEM_CAP);
    lsml_section_t *array;
    LSML_TRY(lsml_data_add_section(data, LSML_ARRAY, "array", 0, &array));
    clock_t t_start = clock();
    for (int i = 1; i < 100000; i++) {
        char buf[256] = {0};
        size_t buf_strlen = snprintf(buf, sizeof(buf)-1, "%d", i);
        LSML_TRY(lsml_array_push(data, array, buf, buf_strlen, 1));
        printf("%d\t%llu\n", i, (unsigned long long)lsml_data_mem_usage(data));
    }
    clock_t clock_total = clock() - t_start;
    double duration = clock_total * (1.0 / CLOCKS_PER_SEC);
    fprintf(stderr, "Total time: %fs", duration);
}