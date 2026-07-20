#include "db_conformance_cache.h"

#include "db_byte_codec.h"
#include "db_core.h"
#include "db_hash.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    DB_CONFORMANCE_CACHE_SCHEMA = 1U,
    DB_CONFORMANCE_CACHE_MAGIC_BYTES = 4U,
    DB_CONFORMANCE_CACHE_RESULT_OFFSET = 12U,
    DB_CONFORMANCE_CACHE_HEADER_BYTES = 32U,
    DB_CONFORMANCE_CACHE_TEMP_SUFFIX_BYTES = 16U,
};

#define DB_CONFORMANCE_CACHE_DOMAIN UINT32_C(0x434F4E46)

static uint64_t cache_checksum(const void *key, size_t key_size,
                               db_conformance_result_t result) {
    const uint64_t key_hash = db_fnv1a64_tree(
        key, key_size, DB_CONFORMANCE_CACHE_DOMAIN, DB_FNV1A64_OFFSET);
    return db_fnv1a64_mix_u64(key_hash, (uint64_t)result);
}

static int fsync_parent_directory(const char *path) {
    if (path == NULL) {
        return 0;
    }
    const char *const separator = strrchr(path, '/');
    size_t prefix_length = 0U;
    if (separator != NULL) {
        prefix_length = db_checked_ptrdiff_to_size(
            "conformance_cache", "parent_directory_prefix", separator - path);
    }
    const size_t directory_length = (prefix_length > 0U) ? prefix_length : 1U;
    size_t allocation_size = 0U;
    if (db_try_add_size(directory_length, 1U, &allocation_size) == 0) {
        return 0;
    }
    char *const directory = malloc(allocation_size);
    if (directory == NULL) {
        return 0;
    }
    if (separator == NULL) {
        directory[0] = '.';
    } else if (prefix_length == 0U) {
        directory[0] = '/';
    } else {
        memcpy(directory, path, prefix_length);
    }
    directory[directory_length] = '\0';
    const int directory_fd = open(directory, O_RDONLY);
    free(directory);
    if (directory_fd < 0) {
        return 0;
    }
    const int synchronized = fsync(directory_fd) == 0;
    const int closed = close(directory_fd) == 0;
    return synchronized && closed;
}

const char *db_conformance_result_name(db_conformance_result_t result) {
    switch (result) {
    case DB_CONFORMANCE_UNTESTED:
        return "untested";
    case DB_CONFORMANCE_CONFORMING:
        return "conforming";
    case DB_CONFORMANCE_NONCONFORMING:
        return "nonconforming";
    }
    return "unknown";
}

const char *
db_conformance_cache_status_name(db_conformance_cache_status_t status) {
    switch (status) {
    case DB_CONFORMANCE_CACHE_MISS:
        return "miss";
    case DB_CONFORMANCE_CACHE_HIT:
        return "hit";
    case DB_CONFORMANCE_CACHE_INVALID:
        return "invalid";
    case DB_CONFORMANCE_CACHE_IO_ERROR:
        return "io_error";
    }
    return "unknown";
}

int db_conformance_cache_disabled(void) {
    const char *const value = getenv("DRIVERBENCH_DISABLE_PROBE_CACHE");
    return (value != NULL) && (strcmp(value, "1") == 0);
}

db_conformance_cache_status_t
db_conformance_cache_read(const char *path, const void *serialized_key,
                          size_t key_size,
                          db_conformance_result_t *out_result) {
    if ((path == NULL) || (serialized_key == NULL) || (key_size == 0U) ||
        (key_size > DB_CONFORMANCE_CACHE_MAX_KEY_BYTES) ||
        (out_result == NULL) || db_conformance_cache_disabled()) {
        return DB_CONFORMANCE_CACHE_MISS;
    }
    if (access(path, F_OK) != 0) {
        return DB_CONFORMANCE_CACHE_MISS;
    }
    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        return DB_CONFORMANCE_CACHE_IO_ERROR;
    }
    uint8_t header[DB_CONFORMANCE_CACHE_HEADER_BYTES] = {0};
    const int header_ok = fread(header, sizeof(header), 1U, file) == 1U;
    if (header_ok == 0) {
        (void)fclose(file);
        return DB_CONFORMANCE_CACHE_INVALID;
    }
    const uint32_t stored_size = db_load_u32_le(&header[8]);
    const int size_valid = (size_t)stored_size == key_size;
    uint8_t stored_key[DB_CONFORMANCE_CACHE_MAX_KEY_BYTES] = {0};
    const int key_ok =
        size_valid && (fread(stored_key, key_size, 1U, file) == 1U);
    if (key_ok == 0) {
        (void)fclose(file);
        return DB_CONFORMANCE_CACHE_INVALID;
    }
    const int no_trailing_data = fgetc(file) == EOF;
    (void)fclose(file);
    const uint32_t result_value =
        db_load_u32_le(&header[DB_CONFORMANCE_CACHE_RESULT_OFFSET]);
    const db_conformance_result_t result =
        (db_conformance_result_t)result_value;
    const uint64_t expected = cache_checksum(serialized_key, key_size, result);
    const int valid =
        (memcmp(header, "DBPC", DB_CONFORMANCE_CACHE_MAGIC_BYTES) == 0) &&
        (db_load_u32_le(&header[4]) == DB_CONFORMANCE_CACHE_SCHEMA) && key_ok &&
        no_trailing_data &&
        (memcmp(stored_key, serialized_key, key_size) == 0) &&
        (db_load_u64_le(&header[16]) == expected) &&
        ((result == DB_CONFORMANCE_CONFORMING) ||
         (result == DB_CONFORMANCE_NONCONFORMING));
    if (!valid) {
        return DB_CONFORMANCE_CACHE_INVALID;
    }
    *out_result = result;
    return DB_CONFORMANCE_CACHE_HIT;
}

db_conformance_cache_status_t
db_conformance_cache_write(const char *path, const void *serialized_key,
                           size_t key_size, db_conformance_result_t result) {
    if ((path == NULL) || (serialized_key == NULL) || (key_size == 0U) ||
        (key_size > DB_CONFORMANCE_CACHE_MAX_KEY_BYTES) ||
        ((result != DB_CONFORMANCE_CONFORMING) &&
         (result != DB_CONFORMANCE_NONCONFORMING)) ||
        db_conformance_cache_disabled()) {
        return DB_CONFORMANCE_CACHE_IO_ERROR;
    }
    size_t temp_size = 0U;
    if (db_try_add_size(strlen(path), DB_CONFORMANCE_CACHE_TEMP_SUFFIX_BYTES,
                        &temp_size) == 0) {
        return DB_CONFORMANCE_CACHE_IO_ERROR;
    }
    char *const temp_path = malloc(temp_size);
    if (temp_path == NULL) {
        return DB_CONFORMANCE_CACHE_IO_ERROR;
    }
    (void)db_snprintf(temp_path, temp_size, "%s.tmp.XXXXXX", path);
    const int temp_fd = mkstemp(temp_path);
    FILE *const file = (temp_fd >= 0) ? fdopen(temp_fd, "wb") : NULL;
    if (file == NULL) {
        if (temp_fd >= 0) {
            (void)close(temp_fd);
            (void)remove(temp_path);
        }
        free(temp_path);
        return DB_CONFORMANCE_CACHE_IO_ERROR;
    }
    uint8_t header[DB_CONFORMANCE_CACHE_HEADER_BYTES] = {0};
    header[0] = 'D';
    header[1] = 'B';
    header[2] = 'P';
    header[3] = 'C';
    db_store_u32_le(&header[4], DB_CONFORMANCE_CACHE_SCHEMA);
    db_store_u32_le(&header[8], db_checked_size_to_u32("conformance_cache",
                                                       "key_size", key_size));
    db_store_u32_le(&header[DB_CONFORMANCE_CACHE_RESULT_OFFSET],
                    (uint32_t)result);
    db_store_u64_le(&header[16],
                    cache_checksum(serialized_key, key_size, result));
    const int written = (fwrite(header, sizeof(header), 1U, file) == 1U) &&
                        (fwrite(serialized_key, key_size, 1U, file) == 1U) &&
                        (fflush(file) == 0) && (fsync(fileno(file)) == 0);
    const int close_ok = fclose(file) == 0;
    const int renamed = written && close_ok && (rename(temp_path, path) == 0);
    const int directory_synced = renamed && fsync_parent_directory(path);
    if (!renamed) {
        (void)remove(temp_path);
    }
    free(temp_path);
    return directory_synced ? DB_CONFORMANCE_CACHE_HIT
                            : DB_CONFORMANCE_CACHE_IO_ERROR;
}
