#include <stdio.h>
#include <stdlib.h>
#include "lsml.h"
#define LSML_IO_IMPL
#include "lsml_io.h"

static lsml_err_t most_recent_parse_err = LSML_OK;

static int print_parse_error(void *ud, lsml_err_t errcode, lsml_index_t line_no) {
    if (errcode) {
        most_recent_parse_err = errcode;
        fprintf(stderr, "LSML parse error: %s on line %lu\n", lsml_strerr(errcode), (unsigned long)line_no);
    }
    return 0;
}

int main(int argc, const char **argv) {
    size_t mem_cap = 16*1024*1024; // 16MiB
    const char *filename = NULL;
    if (argc > 1) {
        filename = argv[1];
    }
    if (argc > 2) {
        mem_cap = (size_t) strtoull(argv[2], NULL, 0);
    }
    
    FILE *file = filename ? fopen(filename, "rb") : stdin;
    if (file == NULL) {
        fprintf(stderr, "Could not open %s for reading.\n", filename ? filename : "stdin");
        return -1;
    }
    lsml_reader_t reader = lsml_reader_from_stream(file);
    
    void *mem = malloc(mem_cap);
    lsml_data_t *data = lsml_data_new(mem, mem_cap);
    if (data == NULL) {
        fprintf(stderr, "LSML error: %s\n", lsml_strerr(LSML_ERR_OUT_OF_MEMORY));
        return LSML_ERR_OUT_OF_MEMORY;
    }
    
    lsml_parse_options_t options = LSML_PARSE_ALL;
    options.err_log = print_parse_error;

    lsml_err_t err = lsml_parse(data, reader, options);
    if (err) {
        fprintf(stderr, "LSML error: %s\n", lsml_strerr(err));
        return err;
    }

    lsml_writer_t writer = lsml_writer_to_stream(stdout);
    int ascii = 0;
    #ifdef WIN32
    ascii = 1;
    #endif
    err = lsml_write_data(writer, data, ascii);
    if (err) {
        fprintf(stderr, "LSML error: %s\n", lsml_strerr(err));
        return err;
    }

    return most_recent_parse_err;
}