#include "lsml.h"
#include <stdlib.h>
#include <stdio.h>

static const char *markup = ""
"{table} # comment1\n"
"key=value # comment2\n"
"empty value= # comment3\n"
"=empty key\n"
"missing_equals\n"
"unquoted \"quotes\"=probably ambiguous but makes parsing a *lot* easier\n"
"# line comment\n"
"\n"
"[array] text after section on line 8\n"
"{}reference\n"
"{}`escaped\\tref \\U0001F171`\n"
"1, `\\062` \n"
"`\\x33`,4,0.51e1,`\\x`# comment\n"
",,{},🅰🅱CDEF\n"
"\n"
"{  } # empty section name\n"
"if you're seeing this = something went wrong\n"
;

static int print_parse_error(void *ud, lsml_err_t errcode, lsml_index_t line_no) {
    fprintf(stderr, "LSML parse error: %s on line %lu\n", lsml_strerr(errcode), (unsigned long)line_no);
    return 0;
}

static void print_data(FILE *const file, lsml_data_t *const data) {
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
            fprintf(file, "{%s}\n", section_name.str);
            while(lsml_table_next(section, &section_iter, &key, &value)) {
                fprintf(file, "%s = %s\n", key.str, value.str);
            }
        } else if (section_type == LSML_ARRAY) {
            lsml_string_t value;
            size_t row = SIZE_MAX, col;
            fprintf(file, "[%s]", section_name.str);
            while(lsml_array_next_2d(section, &section_iter, &value, &row, &col)) {
                if (col == 0) fprintf(file, "\n%s, ", value.str);
                else fprintf(file, "%s, ",value.str);
            }
            fprintf(file, "\n");
        }
    }
}

static lsml_err_t check_array_lookup(lsml_data_t *data) {
    lsml_section_t *section;
    lsml_string_t value;
    size_t rows, cols;
    lsml_err_t err;
    err = lsml_data_get_section(data, LSML_ARRAY, "array", 0, &section, NULL);
    if (err) return err;
    err = lsml_array_2d_size(section, 1, &rows, &cols);
    if (err) return err;
    fprintf(stderr, "Array 2D size: %llu x %llu\n", (unsigned long long) rows, (unsigned long long) cols);
    err = lsml_array_get_2d(section, 3, 2, &value);
    if (err) return err;
    fprintf(stderr, "Array 2D lookup: [3,2]=%s\n", value.str);
    return LSML_OK;
}

static lsml_err_t check_value_interp(lsml_data_t *data) {
    lsml_section_t *section;
    lsml_string_t value;
    long long ll;
    double d;
    const char *fail[2] = {"", ""};
    lsml_err_t err;
    err = lsml_data_get_section(data, LSML_ARRAY, "array", 0, &section, NULL);
    if (err) return err;
    err = lsml_array_get(section, 6, &value);
    if (err) return err;
    err = LSML_OK;
    if(lsml_toll(value, &ll)) fail[0] = " (out of range)";
    if(lsml_tod(value, &d)) fail[1] = " (out of range)";
    fprintf(stderr, "Converting value %s to:\n\tlong long=%lld%s\n\tdouble=%f%s\n", value.str, ll, fail[0], d, fail[1]);
    return LSML_OK;
}

#define MEM_CAP (1048576)

int main() {
    char *scratch = (char *) calloc(MEM_CAP, 1);
    if (scratch == NULL) {
        fprintf(stderr, "Failed to allocate scratch memory\n");
        return -1;
    }
    lsml_string_t reader_str = lsml_string_init(markup, 0);
    lsml_reader_t reader = lsml_reader_from_string(&reader_str);
    lsml_data_t *data = lsml_data_new(scratch, MEM_CAP);
    if (data == NULL) return -1;
    lsml_parse_options_t options = {
        .err_log = print_parse_error
    };
    lsml_err_t err = lsml_parse(data, reader, options);
    if (err) {
        fprintf(stderr, "LSML error: %s\n", lsml_strerr(err));
        return err;
    }
    // fprintf(stderr, "Valid references? %s\n", lsml_verify_references(data) ? "yes" : "no");
    // fprintf(stderr, "Matching code works? %s\n", lsml_verify_matches_template(data, data, LSML_MATCH_ALL, NULL) ? "yes" : "no");
    size_t mem_used = lsml_data_mem_usage(data);
    fprintf(stderr, "Mem usage after parse: %llu\n", (unsigned long long) mem_used);
    err = check_array_lookup(data);
    if (err) {
        fprintf(stderr, "Array lookup failed: %s\n", lsml_strerr(err));
    }
    err = check_value_interp(data);
    if (err) {
        fprintf(stderr, "Value interpretation failed: %s\n", lsml_strerr(err));
    }

    print_data(stdout, data);
}