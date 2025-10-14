#include "lsml.c"
#include <stdio.h>

#define LSML_TRY(expr) do { lsml_err_t err__ = (expr); if (err__) { lsml_print_line_info("LSML error: %s at %s:%u\n", lsml_strerr(err__), __FILE__, __LINE__); return err__; } } while(0)
#define LSML_ASSERT(expr) do { if(!(expr)) { lsml_print_line_info("LSML assertion failed: %s at %s:%u\n", #expr, __FILE__, __LINE__); return -1; } } while(0)
static void lsml_print_line_info(const char *fmt, const char *expr, const char *file, unsigned int line) {
    fprintf(stderr, fmt, expr, file, line);
}

static void print_mem_usage(lsml_data_t *data, const char *event) {
    unsigned long long mem = lsml_data_mem_usage(data);
    printf("%llu bytes used after %s\n", mem, event);
}

// static void print_alloc_hook(size_t size, size_t align) {
//     printf("Allocating %llu bytes with align %lu\n", (unsigned long long) size, (unsigned long) align);
// }

static void print_data(lsml_data_t *data) {
    lsml_iter_t data_iter = {0};
    lsml_section_t *section;
    lsml_err_t err;
    lsml_section_type_t section_type;
    while (lsml_data_next_section(data, &data_iter, &section, &section_type)) {
        lsml_iter_t section_iter = {0};
        lsml_string_t section_name;
        err = lsml_section_info(section, &section_name, NULL, NULL);
        if (section_type == LSML_TABLE) {
            lsml_string_t key, value;
            printf("{%s}\n", section_name.str);
            while(lsml_table_next(section, &section_iter, &key, &value)) {
                printf("%s = %s\n", key.str, value.str);
            }
        } else if (section_type == LSML_ARRAY) {
            lsml_string_t value;
            printf("[%s]\n", section_name.str);
            while(lsml_array_next(section, &section_iter, &value)) {
                printf("%s\n",value.str);
            }
        }
    }
}

int main() {
    char BUF[8192];
    lsml_data_t *data = lsml_data_new(BUF, sizeof(BUF));
    LSML_ASSERT(data);
    print_mem_usage(data, "data created");
    // printf("size of data+2*cha_chunk = (%llu)+2*(%llu)=(%llu)\n", sizeof(lsml_data_t), sizeof(lsml_cha_chunk_t),
    //     sizeof(lsml_data_t) + 2*sizeof(lsml_cha_chunk_t));

    const char *bruh_static = "BRUH";
    lsml_reg_str_t *s_bruh;
    LSML_TRY(lsml_data_register_string(data, bruh_static, 0, 0, &s_bruh));
    LSML_ASSERT(s_bruh->string.str != bruh_static); // Check that string is owned by data, not static mem
    const char *moment_static = "MOMENT";
    lsml_reg_str_t *s_moment;
    LSML_TRY(lsml_data_register_string(data, moment_static, 0, 0, &s_moment));
    LSML_ASSERT(s_moment->string.str != moment_static);
    {
        char bruhbuf[4] = "BRUH";
        lsml_reg_str_t *s_bruh2;
        LSML_TRY(lsml_data_register_string(data, bruhbuf, sizeof bruhbuf, 0, &s_bruh2));
        LSML_ASSERT(s_bruh2->string.str == s_bruh->string.str); // Check that identical strings are reused
    }
    
    print_mem_usage(data, "BRUH and MOMENT registered");
    
    lsml_section_t *table;
    LSML_TRY(lsml_data_add_section_internal(data, s_bruh, LSML_TABLE, &table));
    LSML_ASSERT(table);
    print_mem_usage(data, "table created");
    
    lsml_section_t *array;
    LSML_TRY(lsml_data_add_section_internal(data, s_moment, LSML_ARRAY, &array));
    LSML_ASSERT(array);
    print_mem_usage(data, "array created");
    
    lsml_section_t *found;
    LSML_TRY(lsml_data_get_section(data, LSML_TABLE, "BRUH", 0, &found, NULL));
    LSML_ASSERT(table == found);
    
    LSML_ASSERT(LSML_ERR_NOT_FOUND == lsml_data_get_section(data, LSML_TABLE, "BRUH MOMENT", 0, &found, NULL));
    LSML_ASSERT(table == found); // Verify that pointer was not modified
    
    print_data(data);
    
    return 0;
}