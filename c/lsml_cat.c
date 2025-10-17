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


int can_seek(FILE *f) {
    long pos = ftell(f);
    if (pos < 0) return 0; // no position tracking
    return (fseek(f, 0, SEEK_CUR) == 0); // if no-op fails, then seeking is likely not possible
}

size_t default_mem_cap() {
    if (sizeof(size_t) >= 8)
        return 1024*1024*1024; // 1GiB
    else if (sizeof(size_t) >= 4) {
        return 128*1024*1024; // 128MiB
    } else if (sizeof(size_t) >= 2) {
        return 16*1024*1024; // 16MiB
    }
    return 1024*1024; // 1MiB
}

int main(int argc, const char **argv) {
    int n_files = 0;
    FILE *file = NULL;
    FILE **files;
    size_t mem_cap = 4096;
    if (argc > 1) {
        files = malloc(sizeof(FILE*)*(argc-1));
        if (files == NULL) {
            fprintf(stderr, "out of memory\n");
            return -1;
        }
        for(int i = 0; i < (argc-1); i++) {
            const char *filename = argv[i+1];
            file = fopen(filename, "rb");
            if (file == NULL) {
                fprintf(stderr, "%s: %s: No such file or directory\n", argv[0], filename);
                i -= 1;
                continue;
            }
            files[n_files] = file;
            n_files += 1;
        }
        // estimate memory capacity
        for (int i = 0; i < n_files; i++) {
            file = files[i];
            if (!can_seek(file)) {
                mem_cap = default_mem_cap();
                break;
            }
            fseek(file, 0, SEEK_END);
            long bytes = ftell(file);
            if (bytes < 0) {
                mem_cap = default_mem_cap();
                rewind(file);
                break;
            }
            rewind(file);
            size_t new_cap = mem_cap + (32*sizeof(void*))*((size_t)bytes);
            if (new_cap < mem_cap) { // overflow
                fprintf(stderr, "out of memory\n");
                return -1;
            }
            mem_cap = new_cap;
        }
    } else {
        // relying on virtual memory on 32-bit and 64-bit systems
        file = stdin;
        files = &file;
        mem_cap = default_mem_cap();
        n_files = 1;
    }

    void *mem = malloc(mem_cap);
    if (mem == NULL) {
        fprintf(stderr, "out of memory\n");
        return -1;
    }
    
    lsml_data_t *data = lsml_data_new(mem, mem_cap);
    if (data == NULL) {
        fprintf(stderr, "out of memory\n");
        return -1;
    }

    lsml_parse_options_t options = LSML_PARSE_ALL;
    options.err_log = print_parse_error;
    for (int i = 0; i < n_files; i++) {
        file = files[i];
        lsml_reader_t reader = lsml_reader_from_stream(files[i]);
        lsml_err_t err = lsml_parse(data, reader, options);
        if (err) {
            fprintf(stderr, "LSML error: %s\n", lsml_strerr(err));
            return err;
        }
        if (file != stdin) fclose(file);
    }

    lsml_writer_t writer = lsml_writer_to_stream(stdout);
    lsml_err_t err = lsml_write_data(writer, data);
    if (err) {
        fprintf(stderr, "LSML error: %s\n", lsml_strerr(err));
        return err;
    }

    free(mem);
    if (files != &file) free(files);

    return 0;
}

int main2(int argc, const char **argv) {
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
    err = lsml_write_data(writer, data);
    if (err) {
        fprintf(stderr, "LSML error: %s\n", lsml_strerr(err));
        return err;
    }

    return most_recent_parse_err;
}