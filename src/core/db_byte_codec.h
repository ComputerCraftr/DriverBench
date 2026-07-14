#ifndef DRIVERBENCH_CORE_DB_BYTE_CODEC_H
#define DRIVERBENCH_CORE_DB_BYTE_CODEC_H

#include <stddef.h>
#include <stdint.h>

enum {
    DB_U32_WIRE_BYTES = 4U,
    DB_U64_WIRE_BYTES = 8U,
};

static inline void db_store_u32_le(uint8_t output[DB_U32_WIRE_BYTES],
                                   uint32_t value) {
    for (uint32_t index = 0U; index < DB_U32_WIRE_BYTES; index++) {
        output[index] = (uint8_t)(value >> (index * 8U));
    }
}

static inline void db_store_u64_le(uint8_t output[DB_U64_WIRE_BYTES],
                                   uint64_t value) {
    for (uint32_t index = 0U; index < DB_U64_WIRE_BYTES; index++) {
        output[index] = (uint8_t)(value >> (index * 8U));
    }
}

static inline uint32_t db_load_u32_le(const uint8_t input[DB_U32_WIRE_BYTES]) {
    uint32_t value = 0U;
    for (uint32_t index = 0U; index < DB_U32_WIRE_BYTES; index++) {
        value |= (uint32_t)input[index] << (index * 8U);
    }
    return value;
}

static inline uint64_t db_load_u64_le(const uint8_t input[DB_U64_WIRE_BYTES]) {
    uint64_t value = 0U;
    for (uint32_t index = 0U; index < DB_U64_WIRE_BYTES; index++) {
        value |= (uint64_t)input[index] << (index * 8U);
    }
    return value;
}

static inline int db_hex_encode_lower(const uint8_t *input, size_t input_size,
                                      char *output, size_t output_size) {
    static const char digits[] = "0123456789abcdef";
    if ((input == NULL) || (output == NULL) ||
        (input_size > (SIZE_MAX - 1U) / 2U) ||
        (output_size < (input_size * 2U) + 1U)) {
        return 0;
    }
    for (size_t index = 0U; index < input_size; index++) {
        const size_t output_index = index * 2U;
        output[output_index] = digits[input[index] >> 4U];
        output[output_index + 1U] = digits[input[index] & UINT8_C(0x0f)];
    }
    output[input_size * 2U] = '\0';
    return 1;
}

#endif
