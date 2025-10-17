// LSML (Listed Sections Markup Language) C implementation
// 
// Parse strings into LSML data, or create LSML data manually.
// 

#ifndef LSML_H
#define LSML_H

#include <stdint.h>
#include <stddef.h>


// ---- API

#ifdef LSML_SHARED_LIB
#if defined(_WIN32) || defined(_WIN64)
    #if defined(LSML_BUILD)
        #define LSML_API __declspec(dllexport)
    #else
        #define LSML_API __declspec(dllimport)
    #endif
#else
    #define LSML_API __attribute__((visibility("default")))
#endif

#else // not dll
#define LSML_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Types

typedef uint32_t lsml_index_t;

// Stores constant string data and with the string's length.
// The string is owned by lsml_data_t, so don't free it or modify the contents of the pointer.
typedef struct lsml_string_t {
    const char *str; // The string is null-terminated for compatability with C standard.
    size_t len; // Length of the string
} lsml_string_t;


// -- Opaque types

// Stores all information about parsed LSML data.
typedef struct lsml_data_t lsml_data_t;

// Stores information about a section of LSML data, either as a table or an array.
typedef struct lsml_section_t lsml_section_t;

// Stores information about iteration.
// Initialize to zero to start iterating.
// NOTE: only use with one iteration function!
typedef struct lsml_iter_t {
    void *chunk;
    void *elem;
    size_t index;
} lsml_iter_t;

// -- Enums

typedef int8_t lsml_section_type_t;
// Constant for table section type
#define LSML_TABLE ((lsml_section_type_t)0)
// Constant for array section type
#define LSML_ARRAY ((lsml_section_type_t)1)
// Constant for any section type (either table or array)
#define LSML_ANYSECTION ((lsml_section_type_t)-1)

// An error value. Zero means everything is OK, but any other value means something went wrong.
// By convention, official error codes are positive, but user-defined error codes are negative.
typedef int8_t lsml_err_t;
typedef enum lsml_errcode_enum {
    LSML_OK=0, // The operation succeeded!
    // System Errors
    LSML_ERR_OUT_OF_MEMORY=1, // A memory allocation failed.
    LSML_ERR_PARSE_ABORTED, // User aborted parsing
    // Data Retrieval Errors
    LSML_ERR_NOT_FOUND=4, // The key or index from a query is not found.
    LSML_ERR_INVALID_DATA, // The given data is not usable.
    LSML_ERR_INVALID_KEY, // The given key is not usable.
    LSML_ERR_INVALID_SECTION, // The given section reference is not usable.
    LSML_ERR_SECTION_TYPE, // The section does not match its expected type.
    // Value Interpretation Errors
    LSML_ERR_VALUE_NULL=16, // The given string or value was null.
    LSML_ERR_VALUE_FORMAT, // The value does not match its expected format.
    LSML_ERR_VALUE_RANGE, // The value does not fit into the allowable range.
    // Parse Errors
    LSML_ERR_MISSING_END_QUOTE=32,
    LSML_ERR_TEXT_INVALID_ESCAPE,
    LSML_ERR_TEXT_OUTSIDE_SECTION,
    LSML_ERR_TEXT_AFTER_END_QUOTE,
    LSML_ERR_TEXT_AFTER_SECTION_HEADER,
    LSML_ERR_SECTION_HEADER_UNCLOSED,
    LSML_ERR_SECTION_NAME_EMPTY,
    LSML_ERR_SECTION_NAME_REUSED,
    LSML_ERR_TABLE_KEY_REUSED,
    LSML_ERR_TABLE_ENTRY_MISSING_EQUALS,
} lsml_errcode_enum;


LSML_API const char *lsml_strerr(lsml_err_t err);

// -- Parsing Types

// Returns a boolean: If the section should be parsed given its name and type.
typedef int (*lsml_parse_condition_fn)(void *userdata, lsml_string_t section_name, lsml_section_type_t section_type);

// Logs an error which occured during parsing.
// If the function returns nonzero, then parsing is aborted, and 
typedef int (*lsml_parse_err_log_fn)(void *userdata, lsml_err_t errcode, lsml_index_t line_no);

typedef struct lsml_parse_options_t {
    size_t n_sections; // Parse up to this many sections, 0=unlimited
    
    lsml_parse_condition_fn condition; // Section condition function
    void *condition_userdata; // Data to be passed to the condition function
    
    lsml_parse_err_log_fn err_log; // Error logging function
    void *err_log_userdata; // Data to be passed to the error logging function
} lsml_parse_options_t;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
static const lsml_parse_options_t LSML_PARSE_ALL = {.n_sections=0};
static const lsml_parse_options_t LSML_PARSE_ONE = {.n_sections=1};
#else
static const lsml_parse_options_t LSML_PARSE_ALL = {0};
static const lsml_parse_options_t LSML_PARSE_ONE = {1};
#endif


typedef struct lsml_reader_t {
    // Reads a single byte from the reader, returning a value from 0-255 (inclusive) if successful.
    // Returns a negative number if EOF has been reached or the read failed.
    int (*read)(void *userdata);
    // Data given to the read function, usually tracks reader state.
    void *userdata;
} lsml_reader_t;

// Built-in parse filters

// Populates given parse options with userdata and condition such that
// every parsed section must exist as a section within the template data.
// Returns INVALID_DATA if either pointer is NULL.
LSML_API lsml_err_t lsml_parse_condition_sections_match(lsml_parse_options_t *options, lsml_data_t *template);



// --- Strings

// Converts an existing string to a string for use with this library.
// If str is NULL, this returns a string with 0 length and a NULL pointer.
// If len is 0, the str is assumed to be null-terminated, and its length is measured with strlen.
LSML_API lsml_string_t lsml_string_init(const char *str, size_t len);



// --- Reading Data


// Creates a new lsml data storage using the provided memory block with fixed size.
// If creation succeeds, the data's pointer is returned. Otherwise, the NULL pointer is returned.
LSML_API lsml_data_t *lsml_data_new(void *buf, size_t size);

// Gets the data's internal buffer and associated size.
// `buf_size` is an optional pointer to be populated with the buffer size.
LSML_API void *lsml_data_buffer(lsml_data_t *data, size_t *buf_size);

// Resets the contents of the data to just after LSML_DATA_NEW.
// Any pointers to content from this data, including strings, sections, and iterators, are invalid after calling this.
// It is not necessary to call this to free a data's buffer, since the data performed no additional allocation.
// Does nothing if the data is NULL.
LSML_API void lsml_data_clear(lsml_data_t *data);

// Returns the bytes of memory currently in use by the data.
LSML_API size_t lsml_data_mem_usage(const lsml_data_t *data);

// Retrieves the number of sections stored within the data.
LSML_API size_t lsml_data_section_count(const lsml_data_t *data);

// Copies values from one data to another.
// Any conflicting keys are resolved by the `overwrite_conflicts` parameter.
// - If true, any sections, key-value pairs, or array entries will replace entries in dest.
// - If false, any of those values remain the same in dest, and the corresponding src values are ignored.
// 
// NOTE: this appends to dest, so call `lsml_data_clear(dest)` first if you want no conflicts.
LSML_API lsml_err_t lsml_data_copy(lsml_data_t *dest, const lsml_data_t *src, int overwrite_conflicts);


// Parses the output of a reader into lsml data until the reader stops.
// Existing information in the data is kept, and newly parsed sections are added.
LSML_API lsml_err_t lsml_parse(lsml_data_t *data, lsml_reader_t reader, lsml_parse_options_t options);



// -- Sections

// Look up a section by name, returning its address and type.
// If name_len is zero, then section_name must be null-terminated.
// Both section_found and section_type are optional: the pointer will not be written to if it is NULL.
// - Section found is written the section pointer matching the query, if name is found and section matches type
// - Section type is written the concrete type of the section, if name is found and regardless of desired section type
// 
// Returns INVALID_DATA if data is not usable.
// Returns NOT_FOUND if section_name is NULL or not present in the data.
// Returns ERR_SECTION_TYPE if desired type is not TABLE, ARRAY, or ANYTYPE
// Returns ERR_SECTION_TYPE if desired type would not match the result of *section_type, if the desired type is TABLE or ARRAY
LSML_API lsml_err_t lsml_data_get_section(const lsml_data_t *data, lsml_section_type_t desired_type, const char *name, size_t name_len, lsml_section_t **section_found, lsml_section_type_t *section_type);

// Populate a list of all sections, optionally of a certain type.
// Section type can be ANYSECTION, TABLE, or ARRAY and will get any, table, or array sections respectively.
// sections stores up to n_sections pointers, and is optional.
// n_sections_avail stores how many sections of the specified type are available, and is optional.
// Returns INVALID_DATA if data is not usable.
// Returns ERR_SECTION_TYPE if section_type is not ANY, TABLE_REF, or ARRAY_REF.
LSML_API lsml_err_t lsml_data_get_sections(const lsml_data_t *data, lsml_section_type_t desired_type, lsml_section_t **sections, size_t n_sections, size_t *n_sections_avail);

// Gets the next section in the data, overwriting the data in the pointers.
// iter is required, but section and section_type are optional.
// The given iterator must be initialized to 0 to start the iteration.
// The resulting section is set to NULL when iteration ends, but the iterator will still be populated (nonzero).
// Returns if iteration continued. If so, both section and section_type are modified (given that they are provided).
LSML_API int lsml_data_next_section(const lsml_data_t *data, lsml_iter_t *iter, lsml_section_t **section, lsml_section_type_t *section_type);

// Creates a new section in the data, returning the created section.
// Returns INVALID_KEY if the string is not given or empty.
// Returns INVALID_DATA if the data is not usable.
// Returns OUT_OF_MEMORY if there is not enough space for this new section.
// Returns SECTION_NAME_REUSED if there is already a section with the given name.
LSML_API lsml_err_t lsml_data_add_section(lsml_data_t *data, lsml_section_type_t desired_type, const char *name, size_t name_len, lsml_section_t **section_created);


// --- Sections

// Gets information about a section, writing it to the optional pointers: name, type, or n_elems
// Returns INVALID_DATA if data is not usable.
// Returns INVALID_SECTION if section is not usable.
LSML_API lsml_err_t lsml_section_info(const lsml_section_t *section, lsml_string_t *name, lsml_section_type_t *type, size_t *n_elems);

// Quick way to get the number of elements in a section.
// Returns 0 if section is NULL.
LSML_API size_t lsml_section_len(const lsml_section_t *section);


// -- Table Sections

// Gets a value from the table by key, if it exists.
// value stores the resulting value, and is optional.
// Returns INVALID_SECTION if the section is NULL or not a table.
// Returns NOT_FOUND if key_name is empty or not present.
LSML_API lsml_err_t lsml_table_get(const lsml_section_t *table, const char *key_name, size_t key_len, lsml_string_t *value);

// Creates a new entry in the table associated with data. 
// Returns INVALID_DATA if the data is not usable.
// Returns INVALID_SECTION if the section is not usable.
// Returns INVALID_KEY if the string is not given or empty.
// Returns OUT_OF_MEMORY if there is not enough space for this new entry.
// Returns TABLE_KEY_REUSED if there is already an entry with the given key.
LSML_API lsml_err_t lsml_table_add_entry(lsml_data_t *data, lsml_section_t *table, const char *key_name, size_t key_len, const char *value, size_t value_len);

// Gets the next key-value pair from the table, overwriting the data in the pointers.
// Returns if iteration continued. If so, both key and value are modified to contain the next values, if they are present.
// iter is required, but key and value are optional.
// The given iterator must be initialized to 0 to start the iteration.
// The key and value are not modified after iteration ends.
// If the given section is not a table, then this immediately returns 0 and does not modify any of the pointers.
LSML_API int lsml_table_next(const lsml_section_t *table, lsml_iter_t *iter, lsml_string_t *key, lsml_string_t *value);

// -- Array Sections

// Calculates the 2D dimensions of the array.
// - If is_jagged is false, then cols will be set to the minimum column count of all rows.
// - If is_jagged is true, then cols will be set to the maximum column count of all rows.
// 
// NOTE: This must iterate through all rows to find the max or min column size, so don't use this function recklessly!
// Returns INVALID_SECTION if the section is NULL.
// Returns ERR_SECTION_TYPE if the section is not an array.
LSML_API lsml_err_t lsml_array_2d_size(const lsml_section_t *array, int is_jagged, size_t *rows, size_t *cols);

// Gets a value from the array by index, if it exists.
// value stores the resulting value, and is optional.
// Returns INVALID_SECTION if the section is NULL.
// Returns ERR_SECTION_TYPE if the section is not an array.
// Returns NOT_FOUND if the index is out of bounds.
LSML_API lsml_err_t lsml_array_get(const lsml_section_t *array, size_t index, lsml_string_t *value);

// Gets a value from the array by its row and column, if it exists.
// value stores the resulting value, and is optional.
// Returns INVALID_SECTION if the section is NULL.
// Returns ERR_SECTION_TYPE if the section is not an array.
// Returns NOT_FOUND if the row or column is out of bounds.
LSML_API lsml_err_t lsml_array_get_2d(const lsml_section_t *array, size_t row, size_t col, lsml_string_t *value);


LSML_API lsml_err_t lsml_array_find(const lsml_section_t *array, const char *value, size_t value_len, size_t *index);

LSML_API lsml_err_t lsml_array_find_2d(const lsml_section_t *array, const char *value, size_t value_len, size_t *row, size_t *col);

LSML_API lsml_err_t lsml_array_find_in_row(const lsml_section_t *array, const char *value, size_t value_len, size_t row, size_t *col);

LSML_API lsml_err_t lsml_array_find_in_col(const lsml_section_t *array, const char *value, size_t value_len, size_t *row, size_t col);

// Gets multiple values from the array in a range of indices.
// values represents a list of strings at least n_elems long, and is modified to contain pointers and lengths to the elements.
// Returns INVALID_SECTION if the section is NULL.
// Returns ERR_SECTION_TYPE if the section is not an array.
// Returns NOT_FOUND if the index range goes out of bounds, and does not write any data.
LSML_API lsml_err_t lsml_array_get_many(const lsml_section_t *array, size_t start_index, size_t n_elems, lsml_string_t *values);

// Pushes a new value onto the end of the array assocaited with data.
// If newrow is true, the value starts a new row, otherwise the value appends to the current row.
LSML_API lsml_err_t lsml_array_push(lsml_data_t *data, lsml_section_t *array, const char *val, size_t val_len, int newrow);

// Gets the next value from the array, overwriting the data in the pointers.
// Returns if iteration continued. If so, value is modified to contain the next value, if it is present.
// iter is required, but value is optional.
// The given iterator must be initialized to 0 to start the iteration.
// The value is not modified after iteration ends.
// If the given section is not an array, then this immediately returns 0 and does not modify any of the pointers.
LSML_API int lsml_array_next(const lsml_section_t *array, lsml_iter_t *iter, lsml_string_t *value);

// Gets the next value from the array while tracking the row and column, overwriting the data in the pointers.
// Returns if iteration continued. If so, value is modified to contain the next value, if it is present.
// iter is required, but value, row, and col are optional.
// The given iterator must be initialized to 0 to start the iteration.
// The value is not modified after iteration ends.
// If the given section is not an array, then this immediately returns 0 and does not modify any of the pointers.
LSML_API int lsml_array_next_2d(const lsml_section_t *array, lsml_iter_t *iter, lsml_string_t *value, size_t *row, size_t *col);

// --- IO


// Creates a reader that reads from a string, incrementing the string's pointer to track progress.
// NOTE: modifies the string's pointer and length, so keep a separate copy!
// Reads from the string until it reaches the end, so the given pointer must exist longer than the reader.
LSML_API lsml_reader_t lsml_reader_from_string(lsml_string_t *string);


// --- Values

// Parses a string into a boolean, writing 0 or 1 to `val` if successful.
// A boolean string must be EXACTLY one of these, otherwise a ERR_FORMAT is returned:
// - "true", "True", "TRUE"
// - "false", "False", "FALSE"
LSML_API lsml_err_t lsml_tobool(lsml_string_t str, int *val);

// Parses a string into an integer, writing to `val`.
// Parses using a base specified by the string prefix:
// - Default is base 10
// - "0x" or "0X" is base 16
// - "0o" or "0O" is base 8
// - "0b" or "0B" is base 2
// If the error returned is ERR_VALUE_RANGE, val is still set, it is just clamped to the appropriate range.
LSML_API lsml_err_t lsml_toi(lsml_string_t str, int *val);
LSML_API lsml_err_t lsml_tol(lsml_string_t str, long *val);
LSML_API lsml_err_t lsml_toll(lsml_string_t str, long long *val);
LSML_API lsml_err_t lsml_tou(lsml_string_t str, unsigned int *val);
LSML_API lsml_err_t lsml_toul(lsml_string_t str, unsigned long *val);
LSML_API lsml_err_t lsml_toull(lsml_string_t str, unsigned long long *val);

// Parses a string into a floating point value, writing to `val`.
// If the error returned is ERR_VALUE_RANGE, val is set to -HUGE_VAL, 0, or HUGE_VAL, as appropriate.
LSML_API lsml_err_t lsml_tof(lsml_string_t str, float *val);
LSML_API lsml_err_t lsml_tod(lsml_string_t str, double *val);

// Parses a string from the lsml_data into a reference, retrieving the referenced section name and type.
//
// NOTE: the characters after the prefix "{}" or "[]" are not parsed, they are used literally:
// - Example: `"{}\"name\""` is NOT interpreted as `"{}name"`, it is used literally.
// Returns ERR_VALUE_FORMAT if the first non-whitespace characters in the string are not "{}" or "[]" exactly.
LSML_API lsml_err_t lsml_toref(lsml_string_t str, lsml_string_t *ref_name, lsml_section_type_t *ref_type);



#ifdef __cplusplus
}
#endif

#endif // LSML_H