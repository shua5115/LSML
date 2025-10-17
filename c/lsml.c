#include "lsml.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- CONFIG

#ifndef LSML_CHUNK_LEN
// Number of elements in a chunk of a chunked array ("cha")
// Larger values result in potentially better performance,
// but with more initial memory usage and possible wasted space.
#define LSML_CHUNK_LEN (8*sizeof(void*))

// Define this ONLY if LSML_CHUNK_LEN is a power of 2
// this allows optimizing integer modulo, helpful for embedded systems
#define LSML_CHUNK_LEN_IS_POW2
// Note: the sizeof(void*) is a power of 2 on *most* systems, but not all.
// It is a safe default, but double check it if your system architecture is unusual.
#endif

// Can be defined as 1 or 2.
// If not defined as 1 or 2, the implementation will use a load factor of 0.8
// #define LSML_LOAD_FACTOR 1


// --- Invariants and Conventions
//
// - All allocations are done through the bump allocator
// - Any pointer returned by a lsml function will never be invalidated (no use-after-free possible)
// - Read only operations on an LSML data should be able to succeed even after running out of memory
// - All lsml_reg_str_t are unique, and pointers to them are unique
// - All lsml_string_t retrieved from lsml_data are null-terminated
//   - Strings passed in by the user are not necessarily null-terminated


// --- Macros

// I just want to know the alignment... :(
// First: check common compiler-specific extensions
// Second: check if C/C++ version supports alignof officially
// Third: fallbacks
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 3)
    #define LSML_ALIGNOF(TYPE) __alignof__(TYPE)
#elif defined(_MSC_VER) && _MSC_VER >= 1300
    #define LSML_ALIGNOF(TYPE) __alignof(TYPE)
#elif defined(__cplusplus) && __cplusplus >= 201103L // C++11 or newer
    #define LSML_ALIGNOF(TYPE) alignof(TYPE)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L // C23 or newer
    #define LSML_ALIGNOF(TYPE) alignof(TYPE)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L // C11 or newer
    #define LSML_ALIGNOF(TYPE) _Alignof(TYPE)
#elif !defined(LSML_DISABLE_OFFSETOF) && !defined(__STRICT_ANSI__) && !defined(__cplusplus) && !defined(__TINYC__) // offsetof fallback
    #warning "LSML: Using offsetof to detect alignment, this may cause undefined behavior on your system. To disable, define \"LSML_DISABLE_OFFSETOF\" or \"__STRICT_ANSI__\"."
    #include <stddef.h> // defines the offsetof macro
    #define LSML_ALIGNOF(TYPE) offsetof(struct { char c; TYPE member; }, member)
#else // maximum alignment fallback
    // This is a large enough alignment for any LSML data type.
    #define LSML_ALIGNOF(TYPE) sizeof(lsml_max_align_t)
#endif

// Using sizeof on an anonymous type definition is a warning for MSVC,
// so a type is created to avoid this warning.
typedef union{size_t s;void *p;} lsml_max_align_t;

// --- Types

typedef struct lsml_bump_alloc_t {
    char * mem;
    size_t offset;
    size_t size;
} lsml_bump_alloc_t;

// Registered string (stores hash)
typedef struct lsml_reg_str_t {
    lsml_string_t string;
    lsml_index_t hash;
} lsml_reg_str_t;

// Common header of entries inside a hashmap
typedef struct lsml_hm_node_t {
    struct lsml_hm_node_t *next;
    lsml_reg_str_t *str;
} lsml_hm_node_t;

// Common layout of a chunk in a chunked array ("cha")
// All chunked arrays must STRICTLY be a pointer to the next chunk and an array of pointers of length LSML_CHUNK_LEN!
typedef struct lsml_cha_chunk_t {
    struct lsml_cha_chunk_t *next;
    void *elems[LSML_CHUNK_LEN];
} lsml_cha_chunk_t;


typedef struct lsml_array_chunk_t {
    struct lsml_array_chunk_t *next;
    lsml_string_t *elems[LSML_CHUNK_LEN];
} lsml_array_chunk_t;

typedef struct lsml_rows_index_t {
    struct lsml_rows_index_t *next;
    size_t index;
} lsml_rows_index_t;


typedef struct lsml_table_node_t {
    lsml_hm_node_t node;
    lsml_string_t *value;
} lsml_table_node_t;

typedef struct lsml_table_chunk_t {
    struct lsml_table_chunk_t *next;
    lsml_table_node_t *buckets[LSML_CHUNK_LEN];
} lsml_table_chunk_t;


struct lsml_section_t {
    lsml_hm_node_t node;
    union {
        lsml_table_chunk_t *table;
        lsml_array_chunk_t *array;
    } section;
    union {
        lsml_table_chunk_t *table;
        lsml_array_chunk_t *array;
    } last_chunk;
    size_t n_elems;
    size_t n_chunks;
    lsml_rows_index_t *row_indices; // If NULL, then this section is a table, otherwise it is an array.
    lsml_rows_index_t *last_row_index;
};


typedef struct lsml_section_chunk_t {
    struct lsml_section_chunk_t *next;
    lsml_section_t *buckets[LSML_CHUNK_LEN];
} lsml_section_chunk_t;

typedef struct lsml_strings_chunk_t {
    struct lsml_strings_chunk_t *next;
    lsml_hm_node_t *buckets[LSML_CHUNK_LEN];
} lsml_strings_chunk_t;


struct lsml_data_t {
    // bump allocator
    lsml_bump_alloc_t alloc;

    // section hashmap chunks
    lsml_section_chunk_t *sections_head;
    lsml_section_chunk_t *sections_tail;
    size_t n_sections;
    size_t n_section_chunks;

    // strings hashmap chunks
    lsml_strings_chunk_t *strings_head;
    lsml_strings_chunk_t *strings_tail;
    size_t n_strings;
    size_t n_strings_chunks;
};


static void *lsml_bump_alloc(lsml_bump_alloc_t *alloc, size_t size, size_t align) {
    size_t aligned_offset = (alloc->offset + (align-1)) & ~(align-1);
    if (aligned_offset + size >= alloc->size) return NULL;
    void *ptr = alloc->mem + aligned_offset;
    alloc->offset = aligned_offset + size;
    return ptr;
}

static inline int lsml_data_owns_ptr(lsml_data_t *data, const void *ptr) {
    return (const char*)ptr >= data->alloc.mem && (const char*)ptr < data->alloc.mem+data->alloc.size;
}

const char *lsml_strerr(lsml_err_t err) {
    switch (err) {
        case LSML_OK: return "";
        // System Errors
        case LSML_ERR_OUT_OF_MEMORY: return "out of memory";
        case LSML_ERR_PARSE_ABORTED: return "parse aborted";
        // Data Retrieval Errors
        case LSML_ERR_NOT_FOUND: return "not found";
        case LSML_ERR_INVALID_DATA: return "invalid data";
        case LSML_ERR_INVALID_KEY: return "invalid key";
        case LSML_ERR_INVALID_SECTION: return "invalid section";
        case LSML_ERR_SECTION_TYPE: return "incorrect section type";
        // Value Interpretation Errors
        case LSML_ERR_VALUE_NULL: return "null value pointer";
        case LSML_ERR_VALUE_FORMAT: return "incorrect value format";
        case LSML_ERR_VALUE_RANGE: return "value out of range";
        // Parse Errors
        case LSML_ERR_MISSING_END_QUOTE: return "missing end quote";
        case LSML_ERR_TEXT_INVALID_ESCAPE: return "invalid escape sequence";
        case LSML_ERR_TEXT_OUTSIDE_SECTION: return "text outside section";
        case LSML_ERR_TEXT_AFTER_END_QUOTE: return "text after end quote";
        case LSML_ERR_TEXT_AFTER_SECTION_HEADER: return "text after section header";
        case LSML_ERR_SECTION_HEADER_UNCLOSED: return "section header unclosed";
        case LSML_ERR_SECTION_NAME_EMPTY: return "section name empty";
        case LSML_ERR_SECTION_NAME_REUSED: return "section name reused";
        case LSML_ERR_TABLE_KEY_REUSED: return "table key reused";
        case LSML_ERR_TABLE_ENTRY_MISSING_EQUALS: return "table entry missing '='";
    }
    return "unknown error";
}

// --- Strings

lsml_string_t lsml_string_init(const char *str, size_t len) {
    lsml_string_t s = {str, len};
    if (s.str == NULL) {
        s.len = 0;
        return s;
    }
    if (s.len == 0) {
        s.len = strlen(s.str);
    }
    return s;
}

static int lsml_string_eq(const lsml_string_t *a, const lsml_string_t *b) {
    if (a->len < b->len) return 0;
    if (a->len > b->len) return 0;
    return memcmp(a->str, b->str, a->len) == 0;
}

// ---- Data Structures

// --- Chunked Array

// Gets the element at `index` in a chunked array.
// If NULL, the element either does not exist, or is not assigned a value (since all cha implementations are assumed to store pointers).
static void * lsml_cha_get(void *header, size_t n_elems, size_t n_chunks, size_t index) {
    lsml_cha_chunk_t *cha = (lsml_cha_chunk_t *)header;
    if (cha == NULL || index >= n_elems || index >= n_chunks*LSML_CHUNK_LEN) return NULL;
    for (; index >= LSML_CHUNK_LEN; index -= LSML_CHUNK_LEN) {
        cha = cha->next;
        if (cha == NULL) return NULL;
    }
    return cha->elems[index];
}

// Gets the pointer to element at `index` in a chunked array within the array's full capacity.
// This is used primarily with hashmaps, since hashmap n_elements is independent of array length.
// If the pointer to element is NULL, then the index is out of bounds of the capacity.
// If the element itself is NULL, it is not assigned a value (since all cha implementations are assumed to store pointers).
static void ** lsml_cha_get_bucket(void *header, size_t n_chunks, size_t index) {
    lsml_cha_chunk_t *cha = (lsml_cha_chunk_t *)header;
    if (cha == NULL || index >= n_chunks*LSML_CHUNK_LEN) return NULL;
    while(index >= LSML_CHUNK_LEN) {
        cha = cha->next;
        if (cha == NULL) return NULL;
        index -= LSML_CHUNK_LEN;
    }
    return cha->elems + index;
}


// --- Hash Map

static lsml_index_t lsml_hash_string(const lsml_string_t *str) {
    // modified from Lua 5.4, can use better hash later
    lsml_index_t l = (lsml_index_t)str->len;
    lsml_index_t h = l; // seed ^ l, but seed=0 b/c no randomness source
    for (; l > 0; l--)
        h ^= ((h<<5) + (h>>2) + (unsigned char)(str->str[l - 1]));
    return h;
}

// Gets the modulo of a % b, where b is a multiple of LSML_CHUNK_LEN.
// If LSML_CHUNK_LEN_IS_POW2 is defined, b must also be a power of 2.
static inline size_t lsml_mod_chunklen(size_t a, size_t b) {
#ifdef LSML_CHUNK_LEN_IS_POW2
    return a & (b-1);
#else
    return a % b;
#endif
}

// `buckets_cha_header` MUST be a cha_chunk_t* storing a type compatible with `lsml_hm_node_t *`.
// Returns a pointer compatible with `lsml_hm_node_t *` if found, NULL if not found.
static void * lsml_hm_get_node(void *buckets_cha_header, size_t n_chunks, lsml_string_t *key) {
    if (buckets_cha_header == NULL || key == NULL) return NULL;
    size_t cap = n_chunks*LSML_CHUNK_LEN;
    size_t index = lsml_mod_chunklen(lsml_hash_string(key), cap);
    void **addr = lsml_cha_get_bucket(buckets_cha_header, n_chunks, index);
    if (addr == NULL) return NULL;
    lsml_hm_node_t *node = (lsml_hm_node_t *) *addr;
    for (; node != NULL; node = node->next) {
        if (lsml_string_eq(&node->str->string, key)) {
            return node;
        }
    }
    return NULL;
}

// `buckets_cha_header` MUST be a cha_chunk_t* storing a type compatible with `lsml_hm_node_t *`.
// Returns a pointer compatible with `lsml_hm_node_t *` if found, NULL if not found.
static void * lsml_hm_get_node_reg(void *buckets_cha_header, size_t n_chunks, lsml_reg_str_t *key) {
    if (buckets_cha_header == NULL || key == NULL) return NULL;
    size_t cap = n_chunks*LSML_CHUNK_LEN;
    size_t index = lsml_mod_chunklen(lsml_hash_string(&key->string), cap);
    void **addr = lsml_cha_get_bucket(buckets_cha_header, n_chunks, index);
    if (addr == NULL) return NULL;
    lsml_hm_node_t *node = (lsml_hm_node_t *) *addr;
    for (; node != NULL; node = node->next) {
        if (node->str == key) { // registered strings are unique, can be compared by pointer
            return node;
        }
    }
    return NULL;
}

// Returns new `lsml_hm_node_t *`-compatible object if it is not found in the hashmap and is able to be created.
// Returns the exitsing node if it is found.
// If was_created is given, it is set to whether the returned node was found (0) or created (1).
static void * lsml_hm_get_or_create_node(lsml_bump_alloc_t *alloc, void *buckets_cha_header, size_t *n_elems, size_t n_chunks, lsml_reg_str_t *key, size_t node_size, size_t node_align, int *was_created) {
    // if (key == NULL || key->str == NULL || key->len == 0) return NULL;
    // assert(key);
    // assert(node_size >= sizeof(lsml_hm_node_t));
    // assert(node_align == LSML_ALIGNOF(lsml_hm_node_t));
    size_t cap = n_chunks*LSML_CHUNK_LEN;
    lsml_index_t hash = key->hash; //lsml_hash_string(&key->string);
    size_t index = lsml_mod_chunklen(hash, cap);
    void **bucket_ptr = lsml_cha_get_bucket(buckets_cha_header, n_chunks, index);
    // assert(bucket_ptr != NULL);
    // if (bucket_ptr == NULL) {
    //     // this should never happen...
    //     return NULL;
    // }
    lsml_hm_node_t *node = (lsml_hm_node_t *) *bucket_ptr;
    lsml_hm_node_t *prevnode = NULL;
    while (node != NULL) {
        // if (lsml_string_eq(node->string, key)) {
        if (node->str == key) {
            // then the node exists
            if (was_created) *was_created = 0;
            return node;
        }
        prevnode = node;
        node = node->next;
    }
    // Node is null, so this key isn't present, so create a new node for it
    node = (lsml_hm_node_t *) lsml_bump_alloc(alloc, node_size, node_align);
    if (node == NULL) return NULL;
    *n_elems += 1;
    memset(node, 0, node_size);
    node->str = key;

    if (prevnode) prevnode->next = node;
    else *bucket_ptr = node;
    if (was_created) *was_created = 1;
    return node;
}

// ONLY called during a rehash, puts node in bucket at its index, appended at the end.
// OVERWRITES next pointer of node to prevent circular references, make sure it is properly unlinked before calling
// Appends a node to a linked list in an arbitrary chunk position
static lsml_err_t lsml_hm_put_node_internal(void *buckets_cha_header, size_t n_chunks, lsml_hm_node_t *node, size_t index) {
    void **bucket_ptr = lsml_cha_get_bucket(buckets_cha_header, n_chunks, index);
    if (bucket_ptr == NULL) return LSML_ERR_NOT_FOUND;
    lsml_hm_node_t *curnode = (lsml_hm_node_t *) *bucket_ptr;
    lsml_hm_node_t *prevnode = NULL;
    while(curnode != NULL) {
        prevnode = curnode;
        curnode = curnode->next;
    }
    if (prevnode) prevnode->next = node;
    else *bucket_ptr = node;
    node->next = NULL; // prevent circular references
    return LSML_OK;
}

// Call before inserting new elements.
// If the number of elements in the hashmap exceeds the load factor, then this doubles the number of the hashmap buckets if possible,
// and shuffles all existing elements into their new bucket
// TODO: remove chunk size and chunk align args after verification that asserts are never hit, since all chunks should have identical layout
static lsml_err_t lsml_hm_rehash_if_needed(lsml_bump_alloc_t *alloc, void *buckets_cha_head, void **buckets_cha_tail, size_t n_elems, size_t *n_chunks) {
    // rehash if over load factor of 0.75
    // count*4/3 > capacity <=> count > 0.75*capacity
    // assert(chunk_size >= sizeof(lsml_cha_chunk_t));
    // assert(chunk_align == LSML_ALIGNOF(lsml_cha_chunk_t));
    // assert(buckets_cha_header);
    // assert(n_elems > 0);
    if (*n_chunks == 0) {
        // then the hashmap is empty, and does not need rehashing
        return LSML_OK;
    }
    size_t old_n_chunks = *n_chunks;
    size_t old_cap = old_n_chunks*LSML_CHUNK_LEN;
    #if LSML_LOAD_FACTOR == 1
    if (n_elems <= old_cap) return LSML_OK;
    #elif LSML_LOAD_FACTOR == 2
    if (n_elems/2 <= old_cap) return LSML_OK;
    #else // load factor of 0.8
    if ((n_elems + (n_elems)/4) <= old_cap) return LSML_OK;
    #endif
    size_t og_offset = alloc->offset;
    lsml_cha_chunk_t *cha = (lsml_cha_chunk_t *)(*buckets_cha_tail);
    // while(cha->next) {
    //     cha = cha->next;
    // }
    lsml_cha_chunk_t *old_last_cha = cha;
    // now cha points to the last element, so add old_nchunks more elements
    for (size_t i = 0; i < old_n_chunks; i++) {
        lsml_cha_chunk_t *newcha = (lsml_cha_chunk_t *) lsml_bump_alloc(alloc, sizeof(lsml_cha_chunk_t), LSML_ALIGNOF(lsml_cha_chunk_t));
        if (newcha == NULL) {
            alloc->offset = og_offset; // free memory
            return LSML_ERR_OUT_OF_MEMORY;
        }
        newcha->next = NULL;
        memset(newcha->elems, 0, sizeof newcha->elems);
        cha->next = newcha;
        cha = newcha;
    }
    // update tail
    (*buckets_cha_tail) = cha;

    size_t new_n_chunks = 2*old_n_chunks;
    size_t new_cap = 2*old_cap;
    // time to adjust every single element!!!
    cha = (lsml_cha_chunk_t *)buckets_cha_head; // start from the beginning
    while(cha) {
        for (size_t chunk_index = 0; chunk_index < LSML_CHUNK_LEN; chunk_index++) {
            lsml_hm_node_t *curnode = (lsml_hm_node_t *) cha->elems[chunk_index];
            lsml_hm_node_t *prevnode = NULL;
            while (curnode) {
                lsml_hm_node_t *nextnode = curnode->next;
                size_t hash = (size_t) curnode->str->hash;
                size_t oldindex = lsml_mod_chunklen(hash, old_cap);
                size_t newindex = lsml_mod_chunklen(hash, new_cap);
                if (oldindex == newindex) {
                    prevnode = curnode;
                    curnode = nextnode;
                    continue;
                } // otherwise, index changed and node needs to be relocated
                // Node needs to be unlinked before placing in its correct spot
                if (prevnode) { // if in the middle of list
                    prevnode->next = nextnode; // link neighbors
                } else { // if first in list
                    cha->elems[chunk_index] = (void *) nextnode; // set head to next element
                }
                lsml_hm_put_node_internal(buckets_cha_head, new_n_chunks, curnode, newindex);

                // because current node was removed, prevnode remains the same
                curnode = nextnode;
            }
        }
        // no need to rehash new cha chunks, they only contain properly placed nodes
        if (cha == old_last_cha) break; // WARNING: removing this causes infinite loops, I can't figure out why
        cha = cha->next;
    }

    *n_chunks = new_n_chunks;
    return LSML_OK;
}

// ---- Reading Data

lsml_data_t *lsml_data_new(void *buf, size_t size) {
    lsml_data_t *data;
    {
        lsml_bump_alloc_t alloc = {(char*) buf, 0, size};
        data = (lsml_data_t*) lsml_bump_alloc(&alloc, sizeof(lsml_data_t), LSML_ALIGNOF(lsml_data_t));
        if (data == NULL) return NULL;
        data->alloc = alloc;
    }
    data->sections_head = (lsml_section_chunk_t *) lsml_bump_alloc(&data->alloc, sizeof(lsml_section_chunk_t), LSML_ALIGNOF(lsml_section_chunk_t));
    data->strings_head = (lsml_strings_chunk_t *) lsml_bump_alloc(&data->alloc, sizeof(lsml_strings_chunk_t), LSML_ALIGNOF(lsml_strings_chunk_t));
    if (data->sections_head == NULL || data->strings_head == NULL) return NULL;
    memset(data->sections_head, 0, sizeof(lsml_section_chunk_t));
    memset(data->strings_head, 0, sizeof(lsml_strings_chunk_t));
    data->sections_tail = data->sections_head;
    data->strings_tail = data->strings_head;
    data->n_sections = 0;
    data->n_section_chunks = 1;
    data->n_strings = 0;
    data->n_strings_chunks = 1;
    return data;
}

void *lsml_data_buffer(lsml_data_t *data, size_t *size_result) {
  if (data == NULL) return NULL;
  if (size_result) *size_result = data->alloc.size;
  return (void *)data->alloc.mem;
}

void lsml_data_clear(lsml_data_t *data) {
    if (data == NULL) return;
    // data offset may not be 0 if original memory buffer was misaligned
    size_t data_offset = (size_t) ((char*)data - data->alloc.mem);
    size_t new_offset = data_offset + sizeof(lsml_data_t);
    data->alloc.offset = new_offset;
}

size_t lsml_data_mem_usage(const lsml_data_t *data) {
    if (data == NULL) return 0;
    return data->alloc.offset;
}

size_t lsml_data_section_count(const lsml_data_t *data) {
    if (data == NULL) return 0;
    return data->n_sections;
}

lsml_err_t lsml_data_copy(lsml_data_t *dest, const lsml_data_t *src, int overwrite_conflicts) {
    if (dest == NULL || src == NULL) return LSML_ERR_INVALID_DATA;
    lsml_iter_t section_iter = {0};
    lsml_section_t *section;
    lsml_section_type_t section_type;
    lsml_string_t key, value;
    while(lsml_data_next_section(src, &section_iter, &section, &section_type)) {
        lsml_iter_t values_iter = {0};
        if (section_type == LSML_TABLE) {
            while(lsml_table_next(section, &values_iter, &key, &value)) {
                // APPEND ENTRY TO DATA
            }
        } else if (section_type == LSML_ARRAY) {
            while(lsml_array_next(section, &values_iter, &value)) {
                // APPEND ENTRY TO DATA
            }
        }
    }
    (void) overwrite_conflicts;
    return LSML_OK;
}

// Registers a string with the data. This has the following effects:
// - The passed string may have its pointer overwritten with an extisting string with equivalent data
// - The data "owns" the string after this operation
// - If move_string is true, then the passed string is not copied and instead becomes owned by the data.
//     - NOTE: if the string is not null-terminated, this will return an error.
//
// NOTE: this does not rehash the strings table, so call hm_rehash_if_needed before/after calling to ensure good performance!
// static lsml_err_t lsml_data_register_string(lsml_data_t *data, lsml_string_t *string) {
static lsml_err_t lsml_data_register_string(lsml_data_t *data, const char *string, size_t string_len, int move_string, lsml_reg_str_t **reg_str) {
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    if (string == NULL) return LSML_ERR_INVALID_KEY;
    lsml_string_t str = lsml_string_init(string, string_len);
    lsml_index_t hash = lsml_hash_string(&str);
    size_t index = lsml_mod_chunklen(hash, data->n_strings_chunks*LSML_CHUNK_LEN);
    void **bucket_ptr = lsml_cha_get_bucket(data->strings_head, data->n_strings_chunks, index);
    // if (bucket_ptr == NULL) return LSML_ERR_NOT_FOUND; // This should never happen, since lsml_mod restricts index to be in-bounds
    lsml_hm_node_t *node = (lsml_hm_node_t *) *bucket_ptr;
    lsml_hm_node_t *prevnode = NULL;
    while (node != NULL) {
        if (lsml_string_eq(&node->str->string, &str)) {
            if (reg_str) *reg_str = node->str;
            return LSML_OK;
        }
        prevnode = node;
        node = node->next;
    }
    // Node is null, so this key isn't present, so create a new node for it and copy string data
    size_t og_offset = data->alloc.offset;
    node = (lsml_hm_node_t *) lsml_bump_alloc(&data->alloc, sizeof(lsml_hm_node_t), LSML_ALIGNOF(lsml_hm_node_t));
    if (node == NULL) return LSML_ERR_OUT_OF_MEMORY;
    lsml_reg_str_t *reg = (lsml_reg_str_t *) lsml_bump_alloc(&data->alloc, sizeof(lsml_reg_str_t), LSML_ALIGNOF(lsml_reg_str_t));
    if (reg == NULL) { data->alloc.offset = og_offset; return LSML_ERR_OUT_OF_MEMORY; }
    reg->hash = hash;
    if (move_string) {
        if (str.str[str.len] != 0) return LSML_ERR_INVALID_KEY;
        reg->string = str;
    } else {
        char *buf;
        buf = (char *) lsml_bump_alloc(&data->alloc, str.len+1, LSML_ALIGNOF(char));
        if (buf == NULL) { data->alloc.offset = og_offset; return LSML_ERR_OUT_OF_MEMORY; }
        memcpy(buf, str.str, str.len);
        buf[str.len] = 0; // null terminator
        reg->string = lsml_string_init(buf, str.len);
    }
    // node init
    node->next = NULL;
    node->str = reg;
    if (prevnode) prevnode->next = node;
    else *bucket_ptr = node;
    data->n_strings += 1;
    if (reg_str) *reg_str = reg;
    return LSML_OK;
}

// Creates a new section with given name and type.
// May return one the following errors:
// - Invalid data: data is NULL
// - Out of memory: no space for new section
// - Section name reused: section of that name already exists
static lsml_err_t lsml_data_add_section_internal(lsml_data_t *data, lsml_reg_str_t *section_name, lsml_section_type_t section_type, lsml_section_t **section) {
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    lsml_err_t err = lsml_hm_rehash_if_needed(&data->alloc, data->sections_head, (void**) &data->sections_tail, data->n_sections, &data->n_section_chunks);
    if (err) return err;
    int was_created = 0;
    lsml_section_t *node = (lsml_section_t *) lsml_hm_get_or_create_node(
        &data->alloc, data->sections_head, &data->n_sections, data->n_section_chunks, section_name,
        sizeof(lsml_section_t), LSML_ALIGNOF(lsml_section_t), &was_created
    );
    if (!was_created) return LSML_ERR_SECTION_NAME_REUSED;
    if (node == NULL) return LSML_ERR_OUT_OF_MEMORY;
    // Removed b/c get_or_create_node memset's to zero
    if (section_type == LSML_ARRAY) {
        node->row_indices = lsml_bump_alloc(&data->alloc, sizeof(lsml_rows_index_t), LSML_ALIGNOF(lsml_rows_index_t));
        if (node->row_indices == NULL) return LSML_ERR_OUT_OF_MEMORY;
        memset(node->row_indices, 0, sizeof(lsml_rows_index_t));
        node->last_row_index = node->row_indices;
    } else {
        node->row_indices = NULL;
        node->last_row_index = NULL;
    }
    if (section) *section = node;
    return LSML_OK;
}

// Creates a new entry in the table with given key and value.
// May return one the following errors:
// - Invalid data: data is NULL
// - Invalid section: section is NULL
// - Section type: given section is not a table
// - Out of memory: no space for new entry
// - Table key reused: key already exists
static lsml_err_t lsml_table_add_entry_internal(lsml_data_t *data, lsml_section_t *table, lsml_reg_str_t *key, lsml_reg_str_t *value) {
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    if (table == NULL) return LSML_ERR_INVALID_SECTION;
    if (table->row_indices != NULL) return LSML_ERR_SECTION_TYPE;
    if (table->section.table == NULL) {
        table->section.table = (lsml_table_chunk_t *) lsml_bump_alloc(&data->alloc, sizeof(lsml_cha_chunk_t), LSML_ALIGNOF(lsml_cha_chunk_t));
        if (table->section.table == NULL) return LSML_ERR_OUT_OF_MEMORY;
        memset(table->section.table, 0, sizeof(lsml_table_chunk_t));
        table->n_chunks = 1;
        table->last_chunk.table = table->section.table;
    }
    lsml_err_t err = lsml_hm_rehash_if_needed(&data->alloc, table->section.table, (void**) &table->last_chunk.table, table->n_elems, &table->n_chunks);
    if (err) return err;
    int was_created = 0;
    lsml_table_node_t *node = (lsml_table_node_t *) lsml_hm_get_or_create_node(
        &data->alloc, table->section.table, &table->n_elems, table->n_chunks, key,
        sizeof(lsml_table_node_t), LSML_ALIGNOF(lsml_table_node_t), &was_created
    );
    if (node == NULL) return LSML_ERR_OUT_OF_MEMORY;
    if (!was_created) return LSML_ERR_TABLE_KEY_REUSED;
    node->value = &value->string;
    // if (entry) *entry = node;
    return LSML_OK;
}

static lsml_err_t lsml_array_add_entry_internal(lsml_data_t *data, lsml_section_t *array, lsml_string_t *value, int newrow) {
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    if (array == NULL) return LSML_ERR_INVALID_SECTION;
    if (array->row_indices == NULL) return LSML_ERR_SECTION_TYPE;
    if (array->section.array == NULL) {
        array->section.array = (lsml_array_chunk_t *) lsml_bump_alloc(&data->alloc, sizeof(lsml_cha_chunk_t), LSML_ALIGNOF(lsml_cha_chunk_t));
        if (array->section.array == NULL) return LSML_ERR_OUT_OF_MEMORY;
        memset(array->section.array, 0, sizeof(lsml_table_chunk_t));
        array->n_chunks = 1;
        array->last_chunk.array = array->section.array;
    }
    
    if (array->n_elems >= (array->n_chunks*LSML_CHUNK_LEN)) {
        lsml_array_chunk_t *cha_new = (lsml_array_chunk_t *) lsml_bump_alloc(&data->alloc, sizeof(lsml_array_chunk_t), LSML_ALIGNOF(lsml_array_chunk_t));
        if (cha_new == NULL) return LSML_ERR_OUT_OF_MEMORY;
        memset(cha_new, 0, sizeof(lsml_array_chunk_t));
        array->last_chunk.array->next = cha_new;
        array->last_chunk.array = cha_new;
        array->n_chunks += 1;
    }
    size_t chunk_index = lsml_mod_chunklen(array->n_elems, LSML_CHUNK_LEN);
    array->last_chunk.array->elems[chunk_index] = value;
    // NOTE: n_elems should be incremented by 1 here, but not doing so saves some arithmetic in the following if-statement:
    if (newrow && array->n_elems > 0) {
        lsml_rows_index_t *new_row_index = (lsml_rows_index_t *) lsml_bump_alloc(&data->alloc, sizeof(lsml_rows_index_t), LSML_ALIGNOF(lsml_rows_index_t));
        if (new_row_index == NULL) return LSML_ERR_OUT_OF_MEMORY;
        new_row_index->index = array->n_elems;
        new_row_index->next = NULL;
        array->last_row_index->next = new_row_index;
        array->last_row_index = new_row_index;
    }
    array->n_elems += 1;
    
    return LSML_OK;
}

// --- Sections

lsml_err_t lsml_data_get_section(const lsml_data_t *data, lsml_section_type_t desired_type, const char *name, size_t name_len, lsml_section_t **section_found, lsml_section_type_t *section_type) {
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    lsml_string_t section_name = lsml_string_init(name, name_len);
    if (section_name.str == NULL) return LSML_ERR_INVALID_KEY;
    lsml_section_t *section = (lsml_section_t *) lsml_hm_get_node(data->sections_head, data->n_section_chunks, &section_name);
    if (section == NULL) return LSML_ERR_NOT_FOUND;
    lsml_section_type_t type = section->row_indices ? LSML_ARRAY : LSML_TABLE;
    if (section_type) *section_type = type;
    if (desired_type != LSML_ANYSECTION && desired_type != type) return LSML_ERR_SECTION_TYPE;
    if (section_found) *section_found = section;
    return LSML_OK;
}

lsml_err_t lsml_data_get_sections(const lsml_data_t *data, lsml_section_type_t desired_type, lsml_section_t **sections, size_t n_sections, size_t *n_sections_avail) {
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    size_t i = 0;
    lsml_section_t *section;
    lsml_section_type_t section_type;
    lsml_iter_t section_iter = {0};
    while(lsml_data_next_section(data, &section_iter, &section, &section_type)) {
        if (desired_type == LSML_ANYSECTION || desired_type == section_type) {
            if (i < n_sections) sections[i] = section;
            else if (n_sections_avail == NULL) break;
            i += 1;
        }
    }
    if (n_sections_avail) *n_sections_avail = i;
    return LSML_OK;
}

int lsml_data_next_section(const lsml_data_t *data, lsml_iter_t *iter, lsml_section_t **section, lsml_section_type_t *section_type) {
    if (data == NULL || iter == NULL) return 0;
    if (iter->chunk == NULL) {
        iter->chunk = data->sections_head;
        iter->index = 0;
        iter->elem = data->sections_head->buckets[0];
    } else if (iter->elem) { // try to go to next node in bucket
        iter->elem = ((lsml_section_t *) iter->elem)->node.next;
    }
    while (iter->elem == NULL) { // try to go to next bucket
        iter->index += 1;
        if (iter->index >= LSML_CHUNK_LEN) {
            void *next = ((lsml_section_chunk_t *) iter->chunk)->next;
            if (next) {
                iter->chunk = next;
                iter->index = 0;
            } else {
                iter->index = LSML_CHUNK_LEN;
                return 0;
            }
        }
        iter->elem = ((lsml_section_chunk_t *) iter->chunk)->buckets[iter->index];
    }
    if (section) *section = (lsml_section_t *) iter->elem;
    if (section_type) *section_type = ((lsml_section_t *) iter->elem)->row_indices ? LSML_ARRAY : LSML_TABLE;
    return 1;
}

lsml_err_t lsml_data_add_section(lsml_data_t *data, lsml_section_type_t desired_type, const char *name, size_t name_len, lsml_section_t **section_created) {
    lsml_reg_str_t *reg_str;
    lsml_string_t string = lsml_string_init(name, name_len);
    lsml_err_t err;
    if (string.len == 0) return LSML_ERR_INVALID_KEY;
    err = lsml_data_register_string(data, name, name_len, 0, &reg_str);
    if (err) return err;
    err = lsml_hm_rehash_if_needed(&data->alloc, data->strings_head, (void**) &data->strings_tail, data->n_strings, &data->n_strings_chunks);
    if (err) return err;
    return lsml_data_add_section_internal(data, reg_str, desired_type, section_created);
}

lsml_err_t lsml_section_info(const lsml_section_t *section, lsml_string_t *name, lsml_section_type_t *type, size_t *n_elems) {
    if (section == NULL) return LSML_ERR_INVALID_SECTION;
    if (name) *name = section->node.str->string;
    if (type) *type = section->row_indices ? LSML_ARRAY : LSML_TABLE;
    if (n_elems) *n_elems = section->n_elems;
    return LSML_OK;
}

size_t lsml_section_len(const lsml_section_t *section) {
    if (section == NULL) return 0;
    return section->n_elems;
}

// -- Table Sections

lsml_err_t lsml_table_get(const lsml_section_t *table, const char *key_name, size_t key_len, lsml_string_t *value) {
    if (table == NULL) return LSML_ERR_INVALID_SECTION;
    // if (table->type != LSML_TABLE) return LSML_ERR_SECTION_TYPE;
    if (table->row_indices != NULL) return LSML_ERR_SECTION_TYPE;
    lsml_string_t key = lsml_string_init(key_name, key_len);
    lsml_table_node_t *node = (lsml_table_node_t *) lsml_hm_get_node(table->section.table, table->n_chunks, &key);
    if (node == NULL) return LSML_ERR_NOT_FOUND;
    if (value) *value = *(node->value);
    return LSML_OK;
}

lsml_err_t lsml_table_add_entry(lsml_data_t *data, lsml_section_t *table, const char *key_name, size_t key_len, const char *value, size_t value_len) {
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    if (!lsml_data_owns_ptr(data, table)) return LSML_ERR_INVALID_SECTION;
    if (value == NULL) return LSML_ERR_VALUE_NULL;
    lsml_string_t key_str = lsml_string_init(key_name, key_len);
    if (key_str.len == 0) return LSML_ERR_INVALID_KEY;
    lsml_reg_str_t *key, *val;
    lsml_err_t err;
    err = lsml_data_register_string(data, key_str.str, key_str.len, 0, &key);
    if (err) return err;
    if(lsml_hm_get_node_reg(table->section.table, table->n_chunks, key)) return LSML_ERR_TABLE_KEY_REUSED;
    err = lsml_data_register_string(data, value, value_len, 0, &val);
    if (err) return err;
    return lsml_table_add_entry_internal(data, table, key, val);
}

int lsml_table_next(const lsml_section_t *table, lsml_iter_t *iter, lsml_string_t *key, lsml_string_t *value) {
    if (table == NULL || iter == NULL || table->section.table == NULL || table->row_indices != NULL) return 0;
    if (iter->chunk == NULL) {
        iter->chunk = table->section.table;
        iter->index = 0; // chunk index
        iter->elem = table->section.table->buckets[0];
    } else if (iter->elem) { // try to go to next node in bucket
        iter->elem = ((lsml_table_node_t *) iter->elem)->node.next;
    }
    while (iter->elem == NULL) { // try to go to next chunk
        iter->index += 1;
        if (iter->index >= LSML_CHUNK_LEN) {
            void *next_chunk = ((lsml_table_chunk_t *) iter->chunk)->next;
            if (next_chunk) {
                iter->chunk = next_chunk;
                iter->index = 0;
            } else {
                iter->index = LSML_CHUNK_LEN;
                return 0;
            }
        }
        iter->elem = ((lsml_table_chunk_t *) iter->chunk)->buckets[iter->index];
    }
    if (key) *key = ((lsml_table_node_t *)iter->elem)->node.str->string;
    if (value) *value = *(((lsml_table_node_t *)iter->elem)->value);
    return 1;
}


// -- Array Sections

lsml_err_t lsml_array_2d_size(const lsml_section_t *array, int is_jagged, size_t *rows, size_t *cols) {
    if (array == NULL) return LSML_ERR_INVALID_SECTION;
    if (array->row_indices == NULL) return LSML_ERR_SECTION_TYPE;
    if (rows == NULL && cols == NULL) return LSML_OK; // no need to waste time
    // need to initialize c to most extreme case
    size_t r = 0, c = is_jagged ? 0 : array->n_elems;
    lsml_rows_index_t *row_index = array->row_indices;

    while(row_index) {
        lsml_rows_index_t *next_row_index = row_index->next;
        size_t cur_cols;
        if (next_row_index) {
            cur_cols = next_row_index->index - row_index->index;
        } else {
            cur_cols = array->n_elems - row_index->index;
        }

        r += 1;
        if (is_jagged ? (cur_cols > c) : (cur_cols < c)) c = cur_cols;

        row_index = next_row_index;
    }

    if (rows) *rows = r;
    if (cols) *cols = c;
    return LSML_OK;
}

lsml_err_t lsml_array_get(const lsml_section_t *array, size_t index, lsml_string_t *value) {
    if (array == NULL) return LSML_ERR_INVALID_SECTION;
    if (array->row_indices == NULL) return LSML_ERR_SECTION_TYPE;
    lsml_string_t *elem = (lsml_string_t *) lsml_cha_get(array->section.array, array->n_elems, array->n_chunks, index);
    if (value) *value = *elem;
    return LSML_OK;
}

lsml_err_t lsml_array_get_2d(const lsml_section_t *array, size_t row, size_t col, lsml_string_t *value) {
    if (array == NULL) return LSML_ERR_INVALID_SECTION;
    if (array->row_indices == NULL) return LSML_ERR_SECTION_TYPE;
    lsml_rows_index_t *row_index = array->row_indices;
    while(row) {
        row_index = row_index->next;
        if (row_index == NULL) return LSML_ERR_NOT_FOUND;
        row--;
    }
    // row is now zero, because it was used to track iteration
    col += row_index->index; // col is now the absolute index into the array
    // check if the column would go into the next row, if so fail
    if (row_index->next && col >= row_index->next->index) return LSML_ERR_NOT_FOUND;
    lsml_string_t *elem = (lsml_string_t *) lsml_cha_get(array->section.array, array->n_elems, array->n_chunks, col);
    if (value) *value = *elem;
    return LSML_OK;
}

lsml_err_t lsml_array_get_many(const lsml_section_t *array, size_t start_index, size_t n_elems, lsml_string_t *values) {
    if (array == NULL) return LSML_ERR_INVALID_SECTION;
    // if (array->type != LSML_ARRAY) return LSML_ERR_SECTION_TYPE;
    if (array->row_indices == NULL) return LSML_ERR_SECTION_TYPE;
    if (start_index >= array->n_elems || (start_index+n_elems) > array->n_elems) return LSML_ERR_NOT_FOUND;
    lsml_iter_t array_iter = {0};
    lsml_string_t *value = NULL;
    size_t i = 0, n = start_index+n_elems;
    while(lsml_array_next(array, &array_iter, value)) {
        if (i >= start_index && i < n) {
            values[i - start_index] = *value;
        }
        i += 1;
    }
    return LSML_OK;
}

lsml_err_t lsml_array_push(lsml_data_t *data, lsml_section_t *array, const char *val, size_t val_len, int newrow) {
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    if (!lsml_data_owns_ptr(data, array)) return LSML_ERR_INVALID_SECTION;
    if (array->row_indices == NULL) return LSML_ERR_SECTION_TYPE;
    if (val == NULL) return LSML_ERR_VALUE_NULL;
    lsml_reg_str_t *val_reg;
    lsml_err_t err;
    err = lsml_data_register_string(data, val, val_len, 0, &val_reg);
    if (err) return err;
    return lsml_array_add_entry_internal(data, array, &val_reg->string, newrow);
}

int lsml_array_next(const lsml_section_t *array, lsml_iter_t *iter, lsml_string_t *value) {
    // if (array == NULL || iter == NULL || array->section.array == NULL || array->type != LSML_ARRAY) return 0;
    if (array == NULL || iter == NULL || array->section.array == NULL || array->row_indices == NULL) return 0;
    if (iter->chunk == NULL) {
        iter->chunk = array->section.array;
        iter->elem = array->section.array->elems[0];
        iter->index = 0;
    } else { // try to go to next element
        iter->index += 1;
        size_t index_wrapped = lsml_mod_chunklen(iter->index, LSML_CHUNK_LEN);
        if (index_wrapped == 0) {
            void *next_chunk = ((lsml_array_chunk_t *) iter->chunk)->next;
            if (next_chunk) {
                iter->chunk = next_chunk;
            } else {
                iter->index -= 1;
                return 0;
            }
        }
        iter->elem = ((lsml_array_chunk_t *) iter->chunk)->elems[index_wrapped];
    }
    if (iter->index >= array->n_elems) return 0;
    if (value) *value = *((lsml_string_t *)iter->elem);
    return 1;
}

int lsml_array_next_2d(const lsml_section_t *array, lsml_iter_t *iter, lsml_string_t *value, size_t *row, size_t *col) {
    lsml_string_t *string = NULL;
    if (array == NULL || iter == NULL || array->section.array == NULL || array->row_indices == NULL) return 0;
    if (iter->chunk == NULL) {
        iter->chunk = array->section.array;
        iter->elem = array->row_indices->next; // NOT an element, instead the NEXT row start
        iter->index = 0;
        string = array->section.array->elems[0];
        if (row) *row = 0;
        if (col) *col = 0;
    } else { // try to go to next element
        iter->index += 1;
        size_t index_wrapped = lsml_mod_chunklen(iter->index, LSML_CHUNK_LEN);
        if (index_wrapped == 0) {
            void *next_chunk = ((lsml_array_chunk_t *) iter->chunk)->next;
            if (next_chunk) {
                iter->chunk = next_chunk;
            } else {
                iter->index -= 1;
                return 0;
            }
        }
        string = ((lsml_array_chunk_t *) iter->chunk)->elems[index_wrapped];
        // if the chunk index is the same index as the NEXT row start index
        if (iter->elem && iter->index == ((lsml_rows_index_t*)iter->elem)->index) {
            if (row) *row += 1;
            if (col) *col = 0;
            iter->elem = ((lsml_rows_index_t*)iter->elem)->next;
        } else {
            if (col) *col += 1; // then we advance the column
        }
    }
    if (iter->index >= array->n_elems) return 0;
    if (value) *value = *string;
    return 1;
}



// --- IO


static int lsml_getc(lsml_reader_t reader) {
    return reader.read(reader.userdata);
}

static int lsml_reader_from_string_getc(void *userdata) {
    if (userdata == NULL) return -1;
    lsml_string_t *src = (lsml_string_t *) userdata;
    if (src->str == NULL || src->len == 0) return -1;
    // must be unsigned char to force value between 0-255
    int c = ((unsigned char) src->str[0]);
    src->str += 1;
    src->len -= 1;
    return c;
}

lsml_reader_t lsml_reader_from_string(lsml_string_t *string) {
    lsml_reader_t reader = {lsml_reader_from_string_getc, string};
    return reader;
}


// ---- Parsing

static int lsml_condition_sections_match(void *userdata, lsml_string_t section_name, lsml_section_type_t section_type) {
    lsml_data_t *data = (lsml_data_t *)userdata;
    if (data == NULL) return 0;
    lsml_section_t *section;
    return LSML_OK == lsml_data_get_section(data, section_type, section_name.str, section_name.len, &section, NULL);
}

lsml_err_t lsml_parse_condition_sections_match(lsml_parse_options_t *options, lsml_data_t *template) {
    if (options == NULL || template == NULL) return LSML_ERR_INVALID_DATA;
    options->condition_userdata = template;
    options->condition = lsml_condition_sections_match;
    return LSML_OK;
}



typedef struct lsml_parser_t {
    lsml_reader_t reader;
    lsml_index_t line;
    int cur;
    int next;
    lsml_parse_err_log_fn log_err;
    void *log_err_userdata;
} lsml_parser_t;

// Logs an error that occurred during parsing, communicating it to the user.
// Returns if the user aborts the parsing operation.
static int lsml_log_err(lsml_parser_t *parser, lsml_err_t errcode) {
    if (parser && parser->log_err && errcode) {
        return parser->log_err(parser->log_err_userdata, errcode, parser->line);
    }
    return 0;
}

// Advance a parser to the next character.
// Returns the *current character* of the parser after advancing.
static inline int lsml_nextchar(lsml_parser_t *parser) {
    int c = parser->next;
    if (parser->cur == '\n') parser->line += 1;
    parser->cur = c;
    parser->next = lsml_getc(parser->reader);
    return c;
}

static int lsml_isspace(int c) {
    switch (c) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
        // case '\v':
        // case '\f':
            return 1;
    }
    return 0;
}

// Skips whitespace characters,
// leaving parser->cur at a non-whitespace character.
static void lsml_skip_whitespace(lsml_parser_t *parser) {
    while (lsml_isspace(parser->cur)) {
        lsml_nextchar(parser);
    }
}

// Skips the rest of the characters in a line,
// leaving parser->cur at the newline.
static void lsml_skip_comment(lsml_parser_t *parser) {
    int c = parser->cur;
    while(c >= 0 && c != '\n') {
        c = lsml_nextchar(parser);
    }
}

// Skips the rest of the characters in a line,
// leaving parser->cur at the start of the next line.
static void lsml_skip_line(lsml_parser_t *parser) {
    int c = parser->cur;
    while(c >= 0 && c != '\n') {
        c = lsml_nextchar(parser);
    }
    if (c == '\n') lsml_nextchar(parser);
}

// Helper function for parsing octal numbers.
// If c is between '0' and '7' (inclusive), it appends the digit to the number in `val`, and returns 1.
// Otherwise, this function returns 0.
static inline int lsml_oct_append(int c, unsigned int *val) {
    if (c >= '0' && c <= '7') {
        *val = (*val<<3) + (c - '0');
    } else return 0;
    return 1;
}

// Helper function for parsing hex numbers.
// If c is between '0' and '7' (inclusive), it appends the digit to the number in `val`, and returns 1.
// Otherwise, this function returns 0.
// Uses uint32_t for use with unicode codepoint parsing
static inline int lsml_hex_append(int c, uint32_t *val) {
    if (c >= '0' && c <= '9') {
        *val = (*val<<4) + (c - '0');
    } else if (c >= 'A' && c <= 'F') {
        *val = (*val<<4) + (10 + c - 'A');
    } else if (c >= 'a' && c <= 'f') {
        *val = (*val<<4) + (10 + c - 'a');
    } else return 0;
    return 1;
}

// Parses a 16-bit or 32-bit unicode codepoint into a temporary string buffer, writing starting at cursor.
// Expects the parser window = ['\\', 'u' or 'U']
// Leaves parser->cur pointing at the character *right after* the unicode escape sequence, if successful.
// If escape sequence is invalid, it writes the in-progress parsed escape sequence to the cursor directly,
// leaving parser->cur at the character after the partially parsed escape sequence.
static lsml_err_t lsml_helper_unicode_parse(lsml_parser_t *parser, char **cursor_ptr, char *const end) {
    // must cast to unsigned char to guarantee value from 0-255
    char scratch[16] = {(unsigned char) parser->cur, (unsigned char) parser->next};
    uint32_t codepoint = 0;
    // parser window = ['\\', 'u' or 'U']
    lsml_nextchar(parser);
    // parser window = ['u' or 'U', ?]
    int len_expect = parser->cur == 'U' ? 10 : 6; // full length of escape sequence
    int i = 2; // index into scratch, tracks how many characters parsed
    while(i < len_expect) {
        int c = lsml_nextchar(parser);
        if (!lsml_hex_append(c, &codepoint)) {
            goto skip_encoding;
        }
        scratch[i] = (unsigned char) c;
        i++;
    }
    // if not skipping encoding, parser->cur now points to last character in hex sequence,
    // but parser->cur must point to the char AFTER the hex sequence
    // when the function returns, so:
    lsml_nextchar(parser); // pass last hex char
    
    // encode unicode codepoint (say that 3 times fast)
    // and thanks wikipedia: https://wikipedia.org/wiki/UTF-8#Description
    if (codepoint <= 0x007F) { // 1-byte
        scratch[0] = (unsigned char) codepoint;
        i = 1;
    } else if (codepoint >= 0x0080u && codepoint <= 0x07FFu) { // 2-byte
        scratch[0] = (unsigned char) (0b11000000u | (0b00011111u & (codepoint >> 6)));
        scratch[1] = (unsigned char) (0b10000000u | (0b00111111u & codepoint));
        i = 2;
    } else if (codepoint >= 0x0800u && codepoint <= 0xFFFFu) { // 3-byte
        scratch[0] = (unsigned char) (0b11100000u | (0b00001111u & (codepoint >> 12)));
        scratch[1] = (unsigned char) (0b10000000u | (0b00111111u & (codepoint >> 6)));
        scratch[2] = (unsigned char) (0b10000000u | (0b00111111u & codepoint));
        i = 3;
    } else if (codepoint >= 0x010000u && codepoint <= 0x10FFFFu) { // 4-byte
        scratch[0] = (unsigned char) (0b11110000u | (0b00000111u & (codepoint >> 18)));
        scratch[1] = (unsigned char) (0b10000000u | (0b00111111u & (codepoint >> 12)));
        scratch[2] = (unsigned char) (0b10000000u | (0b00111111u & (codepoint >> 6)));
        scratch[3] = (unsigned char) (0b10000000u | (0b00111111u & codepoint));
        i = 4;
    } else {
        // invalid codepoint
        if(lsml_log_err(parser, LSML_ERR_TEXT_INVALID_ESCAPE)) return LSML_ERR_PARSE_ABORTED;
    }

    skip_encoding:
    // storing the string literal with length `i` from scratch buffer
    if (*cursor_ptr+i > end) return LSML_ERR_OUT_OF_MEMORY;
    // copy scratch into cursor
    memcpy(*cursor_ptr, scratch, i);
    *cursor_ptr += i;

    return LSML_OK;
}

// Parses a single-line string into a buffer at the end of the bump allocator.
// The string can be unquoted or quoted. Quoted strings handle escape characters.
// If the function succeeds in creating a string, it leaves the parser->cur character at the ending delimiter of the string:
// - The newline for unquoted strings
// - The end quote for quoted strings
// - The end_delim when end_delim is set (nonzero)
//   - If a newline is encountered before the end_delim, then parser->cur will be the newline, and an error will be logged.
//
// Set is_name to true if the string cannot be empty.
// - If the string is empty, returns INVALID_KEY.
//   It is best to check for this and replace with the appropriate in-context error,
//   like EMPTY_TABLE_KEY or EMPTY_SECTION_NAME.
//
// The string may start with a section reference prefix of exactly "{}" or "[]".
// - The prefix is stored into the string, then the string is parsed as either quoted or unquoted.
//   - The prefix does not determine whether the string is parsed as quoted or unquoted.
// - Example: {}hello world -> "{}hello world", {}"\x57" -> "{}W"
//
// USAGE OF TEMPORARY STRINGS
// - NEVER call `discard_temp_string` after a function which may bump-allocate, double check if data argument is const!
// - Call `register_temp_string` to fully move ownership of the string into the data, allowing its use in the rest of the parser.
// - It is unecessary to call `discard_temp_string` if this function fails, since the temporary allocation didn't occur.
static lsml_err_t lsml_parse_temp_string(lsml_data_t *data, lsml_parser_t *parser, lsml_string_t *string, int end_delim, int is_name) {
    size_t og_offset = data->alloc.offset;
    char *start = data->alloc.mem + data->alloc.offset;
    // cursor points one-past last char in new string
    char *cursor = start;
    char *end = data->alloc.mem + data->alloc.size - 1; // 1 before end for null terminator
    string->str = NULL;
    string->len = 0;
    if (cursor >= end) return LSML_ERR_OUT_OF_MEMORY;
    int c = parser->cur;
    int delim = 0;
    for (;;) {
        // If reached end of file or string, just save the string (because of the reference prefix, it may not be empty)
        if (c < 0 || c == '\n' || (end_delim && c == end_delim)) {
            goto save_string;
        }
        // Check for section reference prefix if it is the very first thing in the string
        else if (cursor == start && ((c == '{' && parser->next == '}') || (c == '[' && parser->next == ']'))) {
            // save prefix
            if (cursor+2 > end) return LSML_ERR_OUT_OF_MEMORY;
            *cursor = (unsigned char) c;
            cursor++;
            *cursor = (unsigned char) parser->next;
            cursor++;
            lsml_nextchar(parser); // skip '{' or '['
        }
        // End of empty unquoted string
        else if (end_delim && c == end_delim) {delim = '\n'; break; }
        // Start of a escapable string
        else if (c == '`') { delim = '`'; c = lsml_nextchar(parser); break; }
        // Start of a quoted string
        else if (c == '"' || c == '\'') { delim = c; c = lsml_nextchar(parser); break; }
        // Start of an unquoted string
        else if (!lsml_isspace(c)) { delim = '\n'; break; }
        c = lsml_nextchar(parser);
    }
    if (delim == '\n') { // unquoted string
        for (;;) {
            if (c < 0 || c == '\n' || c == '#' || (end_delim && c == end_delim)) {
                if (c == '#') {
                    lsml_skip_comment(parser);
                }
                // trim ending whitespace
                while (cursor > start && lsml_isspace(*(cursor-1))) {
                    cursor -= 1;
                }
                break;
            }
            // check mem after checking if string is over, maximizing length
            if (cursor >= end) return LSML_ERR_OUT_OF_MEMORY;
            *cursor = (unsigned char) c;
            cursor++;
            c = lsml_nextchar(parser);
        }
    } else if (delim == '"' || delim == '\'') {
        for (;;) {
            if (c < 0 || c == '\n') {
                if(lsml_log_err(parser, LSML_ERR_MISSING_END_QUOTE)) return LSML_ERR_PARSE_ABORTED;
                break;
            }
            if (c == delim) break;
            // check mem after checking if string is over, maximizing length
            if (cursor >= end) return LSML_ERR_OUT_OF_MEMORY;
            *cursor = (unsigned char) c;
            cursor++;
            c = lsml_nextchar(parser);
        }
        // pass end quote
        if (c == delim) {
            c = lsml_nextchar(parser);
        }
        // try to reach end delimiter
        if (c >= 0 && c != '\n' && end_delim && c != end_delim) {
            // then go until end delim is reached
            int logged_err = 0;
            while(c >= 0 && c != '\n' && c != end_delim) {
                if (c == '#') {
                    lsml_skip_comment(parser);
                    break;
                }
                if (!logged_err && !lsml_isspace(c)) {
                    if (lsml_log_err(parser, LSML_ERR_TEXT_AFTER_END_QUOTE)) return LSML_ERR_PARSE_ABORTED;
                    logged_err = 1;
                }
                c = lsml_nextchar(parser);
            }
        }
    } else { // escaped string
        for (;;) {
            if (c < 0 || c == '\n') {
                if(lsml_log_err(parser, LSML_ERR_MISSING_END_QUOTE)) return LSML_ERR_PARSE_ABORTED;
                break;
            }
            if (c == '`') break;
            // check mem after checking if string is over, maximizing length
            if (cursor >= end) return LSML_ERR_OUT_OF_MEMORY;

            if (c == '\\') {
                c = parser->next; // peek next character
                // INVARIANT: after this switch statement, parser->cur should point to the LAST character IN the escape sequence
                // thanks wikipedia: https://wikipedia.org/wiki/Escape_sequences_in_C#Escape_sequences
                switch(c) {
                    // Single char escape sequences
                    case 'a': c=0x07; lsml_nextchar(parser); break;
                    case 'b': c=0x08; lsml_nextchar(parser); break;
                    case 'e': c=0x1B; lsml_nextchar(parser); break;
                    case 'f': c=0x0C; lsml_nextchar(parser); break;
                    case 'n': c=0x0A; lsml_nextchar(parser); break;
                    case 'r': c=0x0D; lsml_nextchar(parser); break;
                    case 't': c=0x09; lsml_nextchar(parser); break;
                    case '\\': c=0x5C; lsml_nextchar(parser); break;
                    case '\'': c=0x27; lsml_nextchar(parser); break;
                    case '"': c=0x22; lsml_nextchar(parser); break;
                    case '`': c=0x60; lsml_nextchar(parser); break;
                    case '?': c=0x3F; lsml_nextchar(parser); break;
                    // /ooo (octal)
                    case '0': case '1': case '2': case '3': case '4':
                    case '5': case '6': case '7': {
                        unsigned int val = parser->next - '0';
                        // parse up to two more characters
                        lsml_nextchar(parser);
                        if (lsml_oct_append(parser->next, &val)) {
                            lsml_nextchar(parser);
                            if (lsml_oct_append(parser->next, &val)) {
                                lsml_nextchar(parser);
                            }
                        }
                        if (val > 255) val = 255;
                        c = (int) val;
                    } break;
                    // \xhh (hex)
                    case 'x': {
                        // parser window = ['\\', 'x']
                        uint32_t val = 0;
                        lsml_nextchar(parser);
                        // parser window = ['x', ?]
                        if (lsml_hex_append(parser->next, &val)) {
                            lsml_nextchar(parser);
                            // parser window = [hex[0], ?]
                            if(lsml_hex_append(parser->next, &val)) {
                                lsml_nextchar(parser);
                                // parser window = [hex[1], ?]
                            }
                            c = (int) val;
                        } else {
                            // invalid hex escape, parse as literal.
                            *cursor = '\\';
                            cursor++;
                            // parser->cur already points to the "next" character, since we looked ahead by 1
                            c = parser->cur;
                            if(lsml_log_err(parser, LSML_ERR_TEXT_INVALID_ESCAPE)) return LSML_ERR_PARSE_ABORTED;
                            // parser window = ['x', ?]
                            continue;
                        }
                    } break;
                    // unicode characters are potentially multi-byte, so use separate function to encode the character
                    case 'u': // \uhhhh (unicode 2-byte codepoint)
                    case 'U': { // \Uhhhhhhhh (unicode 4-byte codepoint)
                        if(lsml_helper_unicode_parse(parser, &cursor, end)) return LSML_ERR_OUT_OF_MEMORY;
                        c = parser->cur;
                        continue;
                    }
                    // if no match, use the literal '\' and do not advance parser
                    default: {
                        c = '\\';
                        if (lsml_log_err(parser, LSML_ERR_TEXT_INVALID_ESCAPE)) return LSML_ERR_PARSE_ABORTED;
                    } break;
                }
            } // if escaped sequence
            *cursor = (unsigned char) c;
            cursor++;
            c = lsml_nextchar(parser);
        }
        // pass end quote
        if (c == delim) {
            c = lsml_nextchar(parser);
        }
        // try to reach end delimiter
        if (c >= 0 && c != '\n' && end_delim && c != end_delim) {
            // then go until end delim is reached
            int logged_err = 0;
            while(c >= 0 && c != '\n' && c != end_delim) {
                if (c == '#') {
                    lsml_skip_comment(parser);
                    break;
                }
                if (!logged_err && !lsml_isspace(c)) {
                    if (lsml_log_err(parser, LSML_ERR_TEXT_AFTER_END_QUOTE)) return LSML_ERR_PARSE_ABORTED;
                    logged_err = 1;
                }
                c = lsml_nextchar(parser);
            }
        }
    }
    save_string:
    // Check zero length
    if (is_name && (cursor == start)) return LSML_ERR_INVALID_KEY;
    *cursor = 0; // null terminator
    string->str = start;
    string->len = (size_t)(cursor - start);
    // the following allocation is already validated by checking that cursor <= end in the loops
    data->alloc.offset = og_offset + string->len + 1; // lock-in string allocation
    return LSML_OK;
}

// Discards memory associated with the temporary string,
// setting data->alloc.offset to the start of the string.
// WARNING: DO NOT CALL THIS AFTER OTHER ALLOCATIONS BESIDES `parse_temp_string`.
static void lsml_discard_temp_string(lsml_data_t *data, lsml_string_t *temp_string) {
    if (temp_string->str == NULL) return;
    data->alloc.offset = (size_t)(temp_string->str - data->alloc.mem);
    temp_string->str = NULL;
    temp_string->len = 0;
}

// Registers a temporary string, returning OUT_OF_MEM of there is no more space for new strings.
// Sets the pointer of the string to the registered string, so its pointer may change if the string wasn't unique.
// If the string wasn't unique, then it is discarded.
static lsml_err_t lsml_register_temp_string(lsml_data_t *data, lsml_string_t *string, lsml_reg_str_t **reg_str) {
    lsml_err_t err = lsml_data_register_string(data, string->str, string->len, 1, reg_str);
    if (err) { lsml_discard_temp_string(data, string); return err; }
    if ((*reg_str)->string.str != string->str) {
        // then string already exists and a new one was not created, so destroy the just-created string
        lsml_discard_temp_string(data, string);
    }
    // make temp string the actual one
    *string = (*reg_str)->string;
    // rehash to keep lookup times good
    return lsml_hm_rehash_if_needed(&data->alloc, data->strings_head, (void**) &data->strings_tail, data->n_strings, &data->n_strings_chunks);
}

static lsml_err_t lsml_parse_section_header(lsml_data_t *data, lsml_parser_t *parser, lsml_section_t **section, lsml_parse_condition_fn cond, void *userdata) {
    lsml_string_t temp;
    lsml_section_type_t type;
    lsml_err_t err;
    int delim;
    *section = NULL; // if anything returns early, the section is skipped
    switch(parser->cur) {
        case '{': delim = '}'; type=LSML_TABLE; break;
        case '[': delim = ']'; type=LSML_ARRAY; break;
        default: return LSML_ERR_SECTION_TYPE;
    }
    lsml_nextchar(parser);
    err = lsml_parse_temp_string(data, parser, &temp, delim, 1);
    if (err == LSML_ERR_INVALID_KEY) err = LSML_ERR_SECTION_NAME_EMPTY;
    if (err) return err;

    if (parser->cur == delim) lsml_nextchar(parser);
    else if (lsml_log_err(parser, LSML_ERR_SECTION_HEADER_UNCLOSED)) return LSML_ERR_PARSE_ABORTED;
    // if the above check failed, parser->cur is likely a newline, so don't skip it to maintain parsing-per-line invariant

    // try to reach end newline
    int c = parser->cur;
    int logged_err = 0;
    while(c >= 0 && c != '\n') {
        if (c == '#') {
            lsml_skip_comment(parser);
            break;
        }
        if (!logged_err && !lsml_isspace(c)) {
            if (lsml_log_err(parser, LSML_ERR_TEXT_AFTER_SECTION_HEADER)) return LSML_ERR_PARSE_ABORTED;
            logged_err = 1;
        }
        c = lsml_nextchar(parser);
    }

    if (cond && !(cond(userdata, temp, type))) {
        // then skip this section, but this skip is valid
        lsml_discard_temp_string(data, &temp);
        return LSML_OK;
    }
    // It's ok to register the string now, because if a section with
    // this string's name exists, the add section will fail,
    // and the string is known to be a duplicate, so no memory is wasted.
    lsml_reg_str_t *name;
    err = lsml_register_temp_string(data, &temp, &name);
    if (err) return err;
    return lsml_data_add_section_internal(data, name, type, section);
}

static lsml_err_t lsml_parse_table_entry(lsml_data_t *data, lsml_parser_t *parser, lsml_section_t *table) {
    lsml_string_t temp_key, temp_val;
    lsml_reg_str_t *key, *val;
    lsml_err_t err;
    // PARSE KEY
    err = lsml_parse_temp_string(data, parser, &temp_key, '=', 0);
    // if (err == LSML_ERR_INVALID_KEY) err = LSML_ERR_TABLE_KEY_EMPTY;
    if (err) return err;
    if (parser->cur == '=') lsml_nextchar(parser);
    else {
        lsml_discard_temp_string(data, &temp_key);
        if (lsml_log_err(parser, LSML_ERR_TABLE_ENTRY_MISSING_EQUALS)) return LSML_ERR_PARSE_ABORTED;
        // skips the entry, not a critical error
        return LSML_OK;
    }

    // OK to register now because if the string is already present,
    // then the string will be discarded,
    // and otherwise, we will need it anyway to insert it into the table.
    err = lsml_register_temp_string(data, &temp_key, &key);
    if (err) return err;
    // Plus, registering the string makes lookup faster.
    lsml_table_node_t *table_node = (lsml_table_node_t *) lsml_hm_get_node_reg(table->section.table, table->n_chunks, key);
    if (table_node) {
        // it's still valid syntax, the entry is just skipped
        if (lsml_log_err(parser, LSML_ERR_TABLE_KEY_REUSED)) return LSML_ERR_PARSE_ABORTED;
        return LSML_OK;
    }

    // PARSE VALUE
    err = lsml_parse_temp_string(data, parser, &temp_val, '\n', 0); // newline delim to force checking text after quoted string
    if (err) return err;
    err = lsml_register_temp_string(data, &temp_val, &val);
    if (err) return err;

    // PUT ENTRY
    return lsml_table_add_entry_internal(data, table, key, val);
}

static lsml_err_t lsml_parse_array_entries(lsml_data_t *data, lsml_parser_t *parser, lsml_section_t *array) {
    lsml_string_t temp_val;
    lsml_reg_str_t *val;
    lsml_err_t err;
    int newrow = 1;
    // PARSE COMMA-SEPARATED VALUE
    while (parser->cur >= 0 && parser->cur != '\n' && parser->cur != '#') {
        err = lsml_parse_temp_string(data, parser, &temp_val, ',', 0);
        if (err) return err;
        err = lsml_register_temp_string(data, &temp_val, &val);
        if (err) return err;
        err = lsml_array_add_entry_internal(data, array, &val->string, newrow);
        if (err) return err;
        newrow = 0; // set to 0 after first loop so the first element starts the row
        
        // pass delimiter
        if (parser->cur == ',') lsml_nextchar(parser);
        if (parser->cur == '\n') break;
        lsml_skip_whitespace(parser);
    }
    return LSML_OK;
}

lsml_err_t lsml_parse(lsml_data_t *data, lsml_reader_t reader, lsml_parse_options_t options) {
    lsml_parser_t parser_data = {0};
    lsml_parser_t *parser = &parser_data;
    lsml_section_t *section = NULL;
    size_t n_sections_parsed = 0;
    int c;
    lsml_err_t err = LSML_OK;
    // Initialize parser
    if (data == NULL) return LSML_ERR_INVALID_DATA;
    if (reader.read == NULL) return LSML_OK; // nothing to read
    parser_data.reader = reader,
    parser_data.line=1,
    parser_data.log_err = options.err_log,
    parser_data.log_err_userdata = options.err_log_userdata,
    lsml_nextchar(parser); // cur = 0, next = first
    c = lsml_nextchar(parser); // c = cur = first, next = second
    while(c >= 0) {
        // INVARIANT: the start of this loop must be the start of a new line (one past the newline character)
        lsml_skip_whitespace(parser);
        c = parser->cur;
        if ((c == '{' && parser->next != '}') || (c == '[' && parser->next != ']')) { // start a section, not a section reference
            // check if enough sections have been parsed
            if (options.n_sections != 0 && n_sections_parsed >= options.n_sections) return LSML_OK;
            n_sections_parsed += 1;
            err = lsml_parse_section_header(data, parser, &section, options.condition, options.condition_userdata);
            switch (err) {
                case LSML_OK:
                    break;
                case LSML_ERR_SECTION_NAME_REUSED:
                case LSML_ERR_SECTION_NAME_EMPTY:
                    // skip the section by setting pointer to NULL
                    section = NULL;
                    // and log the error
                    if (lsml_log_err(parser, err)) return LSML_ERR_PARSE_ABORTED;
                    break;
                // otherwise, the error is probably serious and needs to be propagated
                default:
                    return err;
            }
            // if (section->row_indices) last_row_index = section->row_indices;
        } else if (c == '#') {
            lsml_skip_comment(parser);
        } else if (c >= 0) { // parse an entry
            if (section) { // section started or section isn't skipped
                if (section->row_indices) {
                    err = lsml_parse_array_entries(data, parser, section);
                } else {
                    err = lsml_parse_table_entry(data, parser, section);
                }
                switch (err) {
                    case LSML_OK: break;
                    case LSML_ERR_OUT_OF_MEMORY:
                    case LSML_ERR_PARSE_ABORTED:
                        return err;
                    default: {
                        if (lsml_log_err(parser, err)) return LSML_ERR_PARSE_ABORTED;
                        break;
                    }
                }
            } else if(data->n_sections == 0) { // this entry occurred before any section, this is an error
                if (lsml_log_err(parser, LSML_ERR_TEXT_OUTSIDE_SECTION)) return LSML_ERR_PARSE_ABORTED;
            }
            // if there is no section but there are existing sections,
            // then the section was skipped because of an issue with its name,
            // in which case the error was already logged.
        }
        // INVARIANT: if the parsed value ended on a newline, parser->cur should be left on the newline
        // This is so skip_line doesn't skip the next valid line
        lsml_skip_line(parser);
        c = parser->cur;
    } // while (c>=0)
    return LSML_OK;
}


// --- Value Interpreting

lsml_err_t lsml_tobool(lsml_string_t str, int *val) {
    if (str.str == NULL || val == NULL) return LSML_ERR_VALUE_NULL;
    int matches = str.len == 4 && (memcmp(str.str, "true", 4) == 0 || memcmp(str.str, "True", 4) == 0 || memcmp(str.str, "TRUE", 4) == 0);
    if (matches) {
        *val = 1;
        return LSML_OK;
    }
    matches = str.len == 5 && (memcmp(str.str, "false", 5) == 0 || memcmp(str.str, "False", 5) == 0 || memcmp(str.str, "FALSE", 5) == 0);
    if (matches) {
        *val = 0;
        return LSML_OK;
    }
    return LSML_ERR_VALUE_FORMAT;
}


lsml_err_t lsml_toi(lsml_string_t str, int *val) {
    long long ll = 0;
    lsml_err_t err = lsml_toll(str, &ll);
    if (err != LSML_ERR_VALUE_RANGE) return err;
    if (ll < INT_MIN) {
        *val = INT_MIN;
        return LSML_ERR_VALUE_RANGE;
    } else if (ll > INT_MAX) {
        *val = INT_MAX;
        return LSML_ERR_VALUE_RANGE;
    }
    *val = (int) ll;
    return LSML_OK;
}

lsml_err_t lsml_tol(lsml_string_t str, long *val) {
    long long ll = 0;
    lsml_err_t err = lsml_toll(str, &ll);
    if (err != LSML_ERR_VALUE_RANGE) return err;
    if (ll < LONG_MIN) {
        *val = LONG_MIN;
        return LSML_ERR_VALUE_RANGE;
    } else if (ll > LONG_MAX) {
        *val = LONG_MAX;
        return LSML_ERR_VALUE_RANGE;
    }
    *val = (long) ll;
    return LSML_OK;
}

lsml_err_t lsml_toll(lsml_string_t str, long long *val) {
    if (str.str == NULL || val == NULL) return LSML_ERR_VALUE_NULL;
    char *endptr = NULL;
    int base = 10, negative = 0;
    while(lsml_isspace(str.str[0]) && str.len > 0) {
        str.str++;
        str.len--;
    }
    if (str.len == 0) return LSML_ERR_VALUE_FORMAT;
    if (str.len >= 3 && str.str[0] == '-' && str.str[1] == '0') {
        switch(str.str[2]) {
            case 'x': case 'X': base=16; break;
            case 'o': case 'O': base=8; break;
            case 'b': case 'B': base=2; break;
        }
        if (base != 10) {
            negative = 1;
            str.str += 3;
            str.len -= 3;
        }
    } else if (str.len >= 2 && str.str[0] == '0') {
        switch(str.str[1]) {
            case 'x': case 'X': base=16; break;
            case 'o': case 'O': base=8; break;
            case 'b': case 'B': base=2; break;
        }
        if (base != 10) {
            str.str += 2;
            str.len -= 2;
        }
    }
    errno = 0;
    long long v = strtoll(str.str, &endptr, base);
    if (endptr != str.str && base == 10 && (*endptr == '.' || *endptr == 'e' || *endptr == 'E')) { // probably a float
        double d = strtod(str.str, &endptr);
        if (d > (double)LLONG_MAX) {
            v = LLONG_MAX;
            errno = ERANGE;
        } else if (d < (double)LLONG_MIN) {
            v = LLONG_MIN;
            errno = ERANGE;
        } else {
            v = (long long) d;
            errno = (d == (double)v) ? 0 : ERANGE;
        }
    }
    if (base != 10 && negative) {
        v = -v;
    }
    if (endptr == str.str) return LSML_ERR_VALUE_FORMAT;
    *val = v;
    if (errno == ERANGE) {
        errno = 0;
        return LSML_ERR_VALUE_RANGE;
    }
    return LSML_OK;
}

lsml_err_t lsml_tou(lsml_string_t str, unsigned int *val) {
    unsigned long long ull;
    lsml_err_t err = lsml_toull(str, &ull);
    if (err != LSML_ERR_VALUE_RANGE) return err;
    if (ull > UINT_MAX) {
        *val = UINT_MAX;
        return LSML_ERR_VALUE_RANGE;
    }
    *val = (unsigned int) ull;
    return LSML_OK;
}

lsml_err_t lsml_toul(lsml_string_t str, unsigned long *val) {
    unsigned long long ull;
    lsml_err_t err = lsml_toull(str, &ull);
    if (err != LSML_ERR_VALUE_RANGE) return err;
    if (ull > ULONG_MAX) {
        *val = ULONG_MAX;
        return LSML_ERR_VALUE_RANGE;
    }
    *val = (unsigned long) ull;
    return LSML_OK;
}

lsml_err_t lsml_toull(lsml_string_t str, unsigned long long *val) {
    if (str.str == NULL || val == NULL) return LSML_ERR_VALUE_NULL;
    char *endptr = NULL;
    int base = 10;
    while(lsml_isspace(str.str[0]) && str.len > 0) {
        str.str++;
        str.len--;
    }
    if (str.len == 0) return LSML_ERR_VALUE_FORMAT;
    if (str.len >= 2 && str.str[0] == '0') {
        switch(str.str[1]) {
            case 'x': case 'X': base=16; break;
            case 'o': case 'O': base=8; break;
            case 'b': case 'B': base=2; break;
        }
        if (base != 10) {
            str.str += 2;
            str.len -= 2;
        }
    }
    errno = 0;
    unsigned long long v = strtoull(str.str, &endptr, base);
    if (endptr != str.str && base == 10 && (*endptr == '.' || *endptr == 'e' || *endptr == 'E')) { // probably a float
        double d = strtod(str.str, &endptr);
        if (d > (double)ULLONG_MAX) {
            v = ULLONG_MAX;
            errno = ERANGE;
        } else if (d < 0) {
            v = 0;
            errno = ERANGE;
        } else {
            v = (long long) d;
            errno = (d == (double)v) ? 0 : ERANGE;
        }
    }
    if (endptr == str.str) return LSML_ERR_VALUE_FORMAT;
    *val = v;
    if (errno == ERANGE) {
        errno = 0;
        return LSML_ERR_VALUE_RANGE;
    }
    return LSML_OK;
}

lsml_err_t lsml_tof(lsml_string_t str, float *val) {
    if (str.str == NULL || val == NULL) return LSML_ERR_VALUE_NULL;
    char *endptr = NULL;
    float v = 0;
    int base = 10, negative = 0;
    while(lsml_isspace(str.str[0]) && str.len > 0) {
        str.str++;
        str.len--;
    }
    if (str.len == 0) return LSML_ERR_VALUE_FORMAT;
    if (str.len >= 3 && str.str[0] == '-' && str.str[1] == '0') {
        switch(str.str[2]) {
            case 'x': case 'X': base=16; break;
            case 'o': case 'O': base=8; break;
            case 'b': case 'B': base=2; break;
        }
        if (base != 10) {
            negative = 1;
            str.str += 3;
            str.len -= 3;
        }
    } else if (str.len >= 2 && str.str[0] == '0') {
        switch(str.str[1]) {
            case 'x': case 'X': base=16; break;
            case 'o': case 'O': base=8; break;
            case 'b': case 'B': base=2; break;
        }
        if (base != 10) {
            str.str += 2;
            str.len -= 2;
        }
    }
    errno = 0;
    if (base == 10) {
        v = strtof(str.str, &endptr);
        if (endptr == str.str) return LSML_ERR_VALUE_FORMAT;
    } else {
        long long ll = strtoll(str.str, &endptr, base);
        if (endptr == str.str) return LSML_ERR_VALUE_FORMAT;
        if (negative) ll = -ll;
        if ((float)ll > FLT_MAX) {
            v = FLT_MAX;
            errno = ERANGE;
        } else if ((float)ll < -FLT_MAX) {
            v = -FLT_MAX;
            errno = ERANGE;
        } else {
            v = (float)ll;
        }
    }
    *val = v;
    if (errno == ERANGE) {
        errno = 0;
        return LSML_ERR_VALUE_RANGE;
    }
    return LSML_OK;
}

lsml_err_t lsml_tod(lsml_string_t str, double *val) {
    if (str.str == NULL || val == NULL) return LSML_ERR_VALUE_NULL;
    char *endptr = NULL;
    double v = 0;
    int base = 10, negative = 0;
    while(lsml_isspace(str.str[0]) && str.len > 0) {
        str.str++;
        str.len--;
    }
    if (str.len == 0) return LSML_ERR_VALUE_FORMAT;
    if (str.len >= 3 && str.str[0] == '-' && str.str[1] == '0') {
        switch(str.str[2]) {
            case 'x': case 'X': base=16; break;
            case 'o': case 'O': base=8; break;
            case 'b': case 'B': base=2; break;
        }
        if (base != 10) {
            negative = 1;
            str.str += 3;
            str.len -= 3;
        }
    } else if (str.len >= 2 && str.str[0] == '0') {
        switch(str.str[1]) {
            case 'x': case 'X': base=16; break;
            case 'o': case 'O': base=8; break;
            case 'b': case 'B': base=2; break;
        }
        if (base != 10) {
            str.str += 2;
            str.len -= 2;
        }
    }
    errno = 0;
    if (base == 10) {
        v = strtod(str.str, &endptr);
        if (endptr == str.str) return LSML_ERR_VALUE_FORMAT;
    } else {
        long long ll = strtoll(str.str, &endptr, base);
        if (endptr == str.str) return LSML_ERR_VALUE_FORMAT;
        if (negative) ll = -ll;
        if ((double)ll > DBL_MAX) {
            v = DBL_MAX;
            errno = ERANGE;
        } else if ((double)ll < -DBL_MAX) {
            v = -DBL_MAX;
            errno = ERANGE;
        } else {
            v = (double)ll;
        }
    }
    *val = v;
    if (errno == ERANGE) {
        errno = 0;
        return LSML_ERR_VALUE_RANGE;
    }
    return LSML_OK;
}

lsml_err_t lsml_toref(lsml_string_t str, lsml_string_t *ref_name, lsml_section_type_t *ref_type) {
    if (str.str == NULL) return LSML_ERR_VALUE_NULL;
    const char *cur = str.str;
    const char *end = str.str+str.len;
    while (cur != (end-1) && lsml_isspace((int) *cur)) {
        cur++;
    }
    if (cur == (end-1)
        || (*(cur) == '{' && *(cur+1) != '}')
        || (*(cur) == '[' && *(cur+1) != ']')
    ) return LSML_ERR_VALUE_FORMAT;

    if (ref_type) {
        switch(*cur) {
            case '{': *ref_type = LSML_TABLE; break;
            case '[': *ref_type = LSML_ARRAY; break;
            default: *ref_type = LSML_ANYSECTION; break;
        }
    }

    if (ref_name == NULL) return LSML_OK;

    // skip {} or []
    cur += 2;

    ref_name->str = cur;
    ref_name->len = (size_t)(end - cur);

    return LSML_OK;
}


#ifdef __cplusplus
}
#endif