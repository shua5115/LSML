// Single-header library for using IO with the LSML C implementation.
//
// Define LSML_IO_IMPL in a single .c file before #including this to use the functions:
// ```c
// #define LSML_IO_IMPL
// #include "lsml_io.h"
// ```

#ifndef LSML_IO_H
#define LSML_IO_H

#include <stdio.h>
#include "lsml.h"

#ifdef __cplusplus
extern "C" {
#endif


// Used to store state when reading from a buffer.
// Although this stores more data than necessary for reading from a buffer,
// the user can recover the original pointer, making it less error-prone in practice.
typedef struct lsml_const_buffer_t {
    const void *ptr; // pointer to buffer data
    size_t capacity; // size of buffer in bytes
    size_t index; // index to write to in the buffer
} lsml_const_buffer_t;

// Used to store state when writing to a buffer.
// Although this stores more data than necessary for writing into a buffer,
// the user can recover the original pointer, making it less error-prone in practice.
typedef struct lsml_buffer_t {
    void *ptr; // pointer to buffer data
    size_t capacity; // size of buffer in bytes
    size_t index; // index to write to in the buffer
} lsml_buffer_t;

typedef struct lsml_writer_t {
    // Writes a single byte to the writer, returning number >=0 if successful.
    // Returns a negative number if the write failed.
    int (*write)(void *userdata, unsigned char character);
    // Data given to the write function, usually tracks writer state.
    void *userdata;
} lsml_writer_t;

// Wraps a buffer into a lsml_reader_t.
lsml_reader_t lsml_reader_from_buffer(lsml_const_buffer_t *buffer);

// Wraps a stdio FILE* into a lsml_reader_t.
// The file must be open for reading!
lsml_reader_t lsml_reader_from_stream(FILE *stream);

// Wraps a buffer into a lsml_writer_t.
lsml_writer_t lsml_writer_to_buffer(lsml_buffer_t *buffer);

// Wraps a stdio FILE* into a lsml_writer_t.
// The file must be open for writing!
lsml_writer_t lsml_writer_to_stream(FILE *stream);


// --- Writing Functions


// Writes a section, including its header and contents.
// - Table keys may be out of order
// - Array values will be in the same order, including the structure of rows and columns.
// - If ascii is true, then any valid utf8 characters are converted to codepoint escapes in quoted strings.
// Returns ERR_VALUE_NULL if the writer's write function is NULL.
// Returns INVALID_SECTION if the section is unusable.
// Returns ERR_OUT_OF_MEMORY if the write was incomplete.
lsml_err_t lsml_write_section(lsml_writer_t writer, const lsml_section_t *section, int no_header, int no_contents, int ascii);

// Writes the contents of data to the writer in valid LSML syntax.
// - Sections may be out of order
// - Table keys may be out of order
// - Array values will be in the same order, including the structure of rows and columns.
// - If ascii is true, then any valid utf8 characters are converted to codepoint escapes in quoted strings.
// Returns ERR_VALUE_NULL if the writer's write function is NULL.
// Returns INVALID_DATA if the data is unusable.
// Returns ERR_OUT_OF_MEMORY if the write was incomplete.
lsml_err_t lsml_write_data(lsml_writer_t writer, const lsml_data_t *data, int ascii);


#ifdef __cplusplus
}
#endif

#endif // LSML_STDIO_H

#define LSML_IO_IMPL
#ifdef LSML_IO_IMPL

#ifdef __cplusplus
extern "C" {
#endif


static int lsml_reader_from_buffer_getc(void *userdata) {
    lsml_const_buffer_t *buffer = (lsml_const_buffer_t *) userdata;
    if (buffer == NULL || buffer->ptr == NULL || buffer->index >= buffer->capacity) return -1;
    int c = ((unsigned char*)buffer->ptr)[buffer->index];
    buffer->index += 1;
    return c;
}

lsml_reader_t lsml_reader_from_buffer(lsml_const_buffer_t *buffer) {
    lsml_reader_t reader = {lsml_reader_from_buffer_getc, buffer};
    return reader;
}

static int lsml_reader_from_stream_getc(void *userdata) {
    FILE *file = (FILE*) userdata;
    return fgetc(file);
}

lsml_reader_t lsml_reader_from_stream(FILE *stream) {
    lsml_reader_t reader = {lsml_reader_from_stream_getc, stream};
    return reader;
}



static int lsml_writer_to_buffer_putc(void *userdata, unsigned char c) {
    lsml_buffer_t *buffer = (lsml_buffer_t *) userdata;
    if (buffer == NULL || buffer->ptr == NULL || buffer->index >= buffer->capacity) return -1;
    ((unsigned char*)buffer->ptr)[buffer->index] = c;
    buffer->index += 1;
    return c;
}

lsml_writer_t lsml_writer_to_buffer(lsml_buffer_t *buffer) {
    lsml_writer_t writer = {lsml_writer_to_buffer_putc, buffer};
    return writer;
}


static int lsml_writer_to_stream_putc(void *userdata, unsigned char c) {
    FILE *file = (FILE*) userdata;
    return fputc(c, file);
}

lsml_writer_t lsml_writer_to_stream(FILE *stream) {
    lsml_writer_t writer = {lsml_writer_to_stream_putc, stream};
    return writer;
}


static inline lsml_err_t lsml_putc(lsml_writer_t writer, unsigned char c) {
    return (writer.write(writer.userdata, c) < 0) ? LSML_ERR_OUT_OF_MEMORY : LSML_OK;
}

static unsigned char lsml_int_to_hex(unsigned char val) {
    switch (val) {
        case 0: case 1: case 2: case 3: case 4:
        case 5: case 6: case 7: case 8: case 9:
            return val+'0';
        case 10: case 11: case 12: case 13: case 14: case 15:
            return val+'A'-10;
    }
    return '?';
}

// Returns OUT_OF_MEMORY if write failed
static lsml_err_t lsml_write_quoted(lsml_writer_t writer, lsml_string_t string, int ascii) {
    if(lsml_putc(writer, '"') < 0) return LSML_ERR_OUT_OF_MEMORY;
    for (size_t index = 0; index < string.len; index++) {
        unsigned char c = (unsigned char) string.str[index];
        if (c < 32) { // non-visible ascii
            switch(c) {
                case 0x00: c='0'; break;
                case 0x07: c='a'; break;
                case 0x08: c='b'; break;
                case 0x09: c='t'; break;
                case 0x0A: c='n'; break;
                case 0x0B: c='v'; break;
                case 0x0C: c='f'; break;
                case 0x0D: c='r'; break;
                default: goto write_as_hex;
            }
            if(lsml_putc(writer, '\\') < 0) return LSML_ERR_OUT_OF_MEMORY;
            if(lsml_putc(writer, c) < 0) return LSML_ERR_OUT_OF_MEMORY;
        } else if (c < 128) { // visible ascii
            switch(c) {
                case '"':
                case '\\':
                    break; // escape these visible characters
                default:
                    if(lsml_putc(writer, c) < 0) return LSML_ERR_OUT_OF_MEMORY;
                    continue;
            }
            if(lsml_putc(writer, '\\') < 0) return LSML_ERR_OUT_OF_MEMORY;
            if(lsml_putc(writer, c) < 0) return LSML_ERR_OUT_OF_MEMORY;
        } else if ((c & 0b11100000u) == 0b11000000u && index+1 < string.len) { // 2-byte unicode
            unsigned char c2 = (unsigned char) string.str[index+1];
            if (!ascii) {
                if(lsml_putc(writer, c) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, c2) < 0) return LSML_ERR_OUT_OF_MEMORY;
            } else {
                if(lsml_putc(writer, '\\') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, 'u') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, '0') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c & 0b111100u)>>2)) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex(((c & 0b11u)<<2) | ((c2 & 0110000u)>>6))) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c2 & 0b1111u))) < 0) return LSML_ERR_OUT_OF_MEMORY;
            }
            index += 1;
        } else if ((c & 0b11110000u) == 0b11100000u && index+2 < string.len) { // 3-byte unicode
            unsigned char c2 = (unsigned char) string.str[index+1];
            unsigned char c3 = (unsigned char) string.str[index+2];
            if (!ascii) {
                if(lsml_putc(writer, c) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, c2) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, c3) < 0) return LSML_ERR_OUT_OF_MEMORY;
            } else {
                if(lsml_putc(writer, '\\') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, 'u') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c & 0b1111u))) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c2 & 0b111100u)>>2)) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex(((c2 & 0b11u)<<2) | ((c3 & 0b110000u)>>6))) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c3 & 0b1111u))) < 0) return LSML_ERR_OUT_OF_MEMORY;
            }
            index += 2;
        } else if ((c & 0b11111000u) == 0b11110000u && index+3 < string.len) { // 4-byte unicode
            unsigned char c2 = (unsigned char) string.str[index+1];
            // if codepoint > U+10FFFF
            if ((((c & 0b1111u)<<2) | ((c2 & 0b110000u)>>4)) > 0x10) goto write_as_hex;
            // if codepoint < U+010000
            if ((((c & 0b111u)<<2) | ((c2 & 0b110000u)>>4)) == 0) goto write_as_hex;
            unsigned char c3 = (unsigned char) string.str[index+2];
            unsigned char c4 = (unsigned char) string.str[index+3];
            if (!ascii) {
                if(lsml_putc(writer, c) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, c2) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, c3) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, c4) < 0) return LSML_ERR_OUT_OF_MEMORY;
            } else {
                if(lsml_putc(writer, '\\') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, 'U') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, '0') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, '0') < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c & 0b100u)>>2)) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex(((c & 0b11u)<<2) | ((c2 & 0b110000u)>>4))) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c2 & 0b1111u))) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c3 & 0b111100u)>>2)) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex(((c3 & 0b11u)<<2) | ((c4 & 0b110000)>>4))) < 0) return LSML_ERR_OUT_OF_MEMORY;
                if(lsml_putc(writer, lsml_int_to_hex((c4 & 0b1111u))) < 0) return LSML_ERR_OUT_OF_MEMORY;
            }
            index += 3;
        } else { // probably an invisible ascii or invalid unicode character, just write the byte as hex
            write_as_hex:
            if(lsml_putc(writer, '\\') < 0) return LSML_ERR_OUT_OF_MEMORY;
            if(lsml_putc(writer, 'x') < 0) return LSML_ERR_OUT_OF_MEMORY;
            if(lsml_putc(writer, lsml_int_to_hex(c >> 4)) < 0) return LSML_ERR_OUT_OF_MEMORY;
            if(lsml_putc(writer, lsml_int_to_hex(c & 0b1111u)) < 0) return LSML_ERR_OUT_OF_MEMORY;
        }
    }
    if(lsml_putc(writer, '"') < 0) return LSML_ERR_OUT_OF_MEMORY;
    return LSML_OK;
}

lsml_err_t lsml_write_section(lsml_writer_t writer, const lsml_section_t *section, int no_header, int no_contents, int ascii) {
    lsml_iter_t section_iter = {0};
    lsml_string_t section_name;
    lsml_section_type_t section_type;
    lsml_err_t err;
    if (writer.write == NULL) return LSML_ERR_VALUE_NULL;
    err = lsml_section_info(section, &section_name, &section_type, NULL);
    if (err) return LSML_ERR_INVALID_SECTION;
    if (no_header && no_contents) return LSML_OK; // don't waste time
    if (section_type == LSML_TABLE) {
        lsml_string_t key, value;
        if (!no_header) {
            if (lsml_putc(writer, '{')) goto fail;
            if (lsml_write_quoted(writer, section_name, ascii)) goto fail;
            if (lsml_putc(writer, '}')) goto fail;
            if (lsml_putc(writer, '\n')) goto fail;
        }
        if (!no_contents) {
            while(lsml_table_next(section, &section_iter, &key, &value)) {
                if (lsml_write_quoted(writer, key, ascii)) goto fail;
                if (lsml_putc(writer, '=')) goto fail;
                if (lsml_write_quoted(writer, value, ascii)) goto fail;
                if (lsml_putc(writer, '\n')) goto fail;
            }
        }
    } else if (section_type == LSML_ARRAY) {
        lsml_string_t value;
        size_t row = SIZE_MAX, col;
        if (!no_header) {
            if (lsml_putc(writer, '[')) goto fail;
            if (lsml_write_quoted(writer, section_name, ascii)) goto fail;
            if (lsml_putc(writer, ']')) goto fail;
            if (no_contents) { // include at least one newline
                if(lsml_putc(writer, '\n')) goto fail;
            }
        }
        if (!no_contents) {
            while(lsml_array_next_2d(section, &section_iter, &value, &row, &col)) {
                if (col == 0 && (!no_header || row != 0)) {
                    if(lsml_putc(writer, '\n')) goto fail;
                }
                if (lsml_write_quoted(writer, value, ascii)) goto fail;
                if(lsml_putc(writer, ',')) goto fail;
            }
            if(lsml_putc(writer, '\n')) goto fail;
        }
    } else return LSML_ERR_INVALID_SECTION;
    return LSML_OK;
    fail: return LSML_ERR_OUT_OF_MEMORY;
}

lsml_err_t lsml_write_data(lsml_writer_t writer, const lsml_data_t *data, int ascii) {
    lsml_iter_t data_iter = {0};
    lsml_section_t *section;
    lsml_err_t err;
    if (writer.write == NULL) return LSML_ERR_VALUE_NULL;
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    while (lsml_data_next_section(data, &data_iter, &section, NULL)) {
        err = lsml_write_section(writer, section, 0, 0, ascii);
        if (err) return err;
    }
    return LSML_OK;
}

#ifdef __cplusplus
}
#endif

#endif