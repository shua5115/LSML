#include <stdio.h>
#include <stdlib.h>
#include "lsml.h"
#define LSML_IO_IMPL
#include "lsml_io.h"


static const char *markup = ""
"{table} # comment1\n"
"key=value # comment2\n"
"empty= # comment3\n"
"=line4\n"
"missing_equals\n"
"# line comment\n"
"\n"
"[array] text after section on line 8\n"
"{}reference\n"
"{}\"quoted\\tref \\U0001F171\"\n"
"1, \"\\062\" \n"
"\"\\x33\",4,5,\"\\x\"# comment\n"
"🅰🅱CDEF\n"
"\n"
;

static int print_parse_error(void *ud, lsml_err_t errcode, lsml_index_t line_no) {
    fprintf(stderr, "LSML parse error: %s on line %lu\n", lsml_strerr(errcode), (unsigned long)line_no);
    return 0;
}


#define MEM_CAP (1048576)

int main() {
    lsml_err_t err;
    FILE *file = tmpfile();
    if (file == NULL) {
        fprintf(stderr, "Failed to create temporary file\n");
        return -1;
    }
    if (fputs(markup, file) < 0) return -1;
    rewind(file); // restart for reading
    void *scratch = malloc(MEM_CAP);
    if (scratch == NULL) {
        fprintf(stderr, "Failed to allocate scratch memory\n");
        return -1;
    }
    lsml_data_t *data = lsml_data_new(scratch, MEM_CAP);
    if (data == NULL) {
        fprintf(stderr, "Failed to create data\n");
        return -1;
    }
    lsml_reader_t reader = lsml_reader_from_stream(file);
    lsml_parse_options_t options = {
        .err_log = print_parse_error
    };
    err = lsml_parse(data, reader, options);
    if (err) {
        fprintf(stderr, "LSML error: %s\n", lsml_strerr(err));
        return err;
    }
    lsml_writer_t writer = lsml_writer_to_stream(stdout);
    err = lsml_write_data(writer, data, 1);
    if (err) {
        fprintf(stderr, "LSML error: %s\n", lsml_strerr(err));
        return err;
    }
    return 0;
}