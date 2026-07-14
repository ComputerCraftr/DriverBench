#include "db_hash.h"
#include "db_hash_simd_internal.h"

#include "db_core.h"
#include "db_numeric.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

typedef struct {
    uint64_t hash;
    uint64_t first_leaf;
    uint64_t byte_length;
} db_fnv_tree_node_t;

static_assert(DB_FNV_TREE_LEAF_HEADER_BYTES == (2U + 4U + 8U + 4U));
static_assert(DB_FNV_TREE_PARENT_RECORD_BYTES ==
              (2U + 4U + 4U + (DB_FNV_TREE_PARENT_U64_FIELDS * 8U)));
static_assert(DB_FNV_TREE_UNARY_RECORD_BYTES == (2U + 4U + 4U + (3U * 8U)));
static_assert(DB_FNV_TREE_ROOT_RECORD_BYTES == (2U + 4U + 8U + 8U + 4U + 8U));

uint64_t db_fnv1a64_tree_multiply(uint64_t value) {
    return value + (value << DB_FNV_PRIME_SHIFT_1) +
           (value << DB_FNV_PRIME_SHIFT_4) + (value << DB_FNV_PRIME_SHIFT_5) +
           (value << DB_FNV_PRIME_SHIFT_7) + (value << DB_FNV_PRIME_SHIFT_8) +
           (value << DB_FNV_PRIME_SHIFT_40);
}

static uint64_t db_fnv_tree_extend(uint64_t hash, const uint8_t *bytes,
                                   size_t length) {
    for (size_t index = 0U; index < length; index++) {
        hash = db_fnv1a64_tree_multiply(hash ^ bytes[index]);
    }
    return hash;
}

static size_t db_fnv_tree_put_u8(uint8_t *record, size_t offset,
                                 uint8_t value) {
    record[offset] = value;
    return offset + 1U;
}

static size_t db_fnv_tree_put_u32_le(uint8_t *record, size_t offset,
                                     uint32_t value) {
    for (uint32_t byte = 0U; byte < 4U; byte++) {
        record[offset + byte] = (uint8_t)(value >> (byte * 8U));
    }
    return offset + 4U;
}

static size_t db_fnv_tree_put_u64_le(uint8_t *record, size_t offset,
                                     uint64_t value) {
    for (uint32_t byte = 0U; byte < 8U; byte++) {
        record[offset + byte] = (uint8_t)(value >> (byte * 8U));
    }
    return offset + 8U;
}

static void
db_fnv_tree_encode_leaf_header(uint8_t record[DB_FNV_TREE_LEAF_HEADER_BYTES],
                               uint32_t domain, uint64_t leaf_index,
                               uint32_t payload_length) {
    size_t offset = 0U;
    offset = db_fnv_tree_put_u8(record, offset, DB_FNV_TREE_VERSION);
    offset = db_fnv_tree_put_u8(record, offset, DB_FNV_TREE_LEAF_TAG);
    offset = db_fnv_tree_put_u32_le(record, offset, domain);
    offset = db_fnv_tree_put_u64_le(record, offset, leaf_index);
    (void)db_fnv_tree_put_u32_le(record, offset, payload_length);
}

static void
db_fnv_tree_encode_parent(uint8_t record[DB_FNV_TREE_PARENT_RECORD_BYTES],
                          uint32_t domain, uint32_t level,
                          const db_fnv_tree_node_t *left,
                          const db_fnv_tree_node_t *right) {
    size_t offset = 0U;
    offset = db_fnv_tree_put_u8(record, offset, DB_FNV_TREE_VERSION);
    offset = db_fnv_tree_put_u8(record, offset, DB_FNV_TREE_PARENT_TAG);
    offset = db_fnv_tree_put_u32_le(record, offset, domain);
    offset = db_fnv_tree_put_u32_le(record, offset, level);
    offset = db_fnv_tree_put_u64_le(record, offset, left->first_leaf);
    offset = db_fnv_tree_put_u64_le(record, offset,
                                    left->byte_length + right->byte_length);
    offset = db_fnv_tree_put_u64_le(record, offset, left->byte_length);
    offset = db_fnv_tree_put_u64_le(record, offset, right->byte_length);
    offset = db_fnv_tree_put_u64_le(record, offset, left->hash);
    (void)db_fnv_tree_put_u64_le(record, offset, right->hash);
}

static void
db_fnv_tree_encode_unary(uint8_t record[DB_FNV_TREE_UNARY_RECORD_BYTES],
                         uint32_t domain, uint32_t level,
                         const db_fnv_tree_node_t *child) {
    size_t offset = 0U;
    offset = db_fnv_tree_put_u8(record, offset, DB_FNV_TREE_VERSION);
    offset = db_fnv_tree_put_u8(record, offset, DB_FNV_TREE_UNARY_TAG);
    offset = db_fnv_tree_put_u32_le(record, offset, domain);
    offset = db_fnv_tree_put_u32_le(record, offset, level);
    offset = db_fnv_tree_put_u64_le(record, offset, child->first_leaf);
    offset = db_fnv_tree_put_u64_le(record, offset, child->byte_length);
    (void)db_fnv_tree_put_u64_le(record, offset, child->hash);
}

static uint64_t db_fnv_tree_encode_root(uint32_t domain, uint64_t total_bytes,
                                        uint64_t leaf_count, uint32_t depth,
                                        uint64_t child_hash,
                                        uint64_t initial_hash) {
    uint8_t record[DB_FNV_TREE_ROOT_RECORD_BYTES];
    size_t offset = 0U;
    offset = db_fnv_tree_put_u8(record, offset, DB_FNV_TREE_VERSION);
    offset = db_fnv_tree_put_u8(record, offset, DB_FNV_TREE_ROOT_TAG);
    offset = db_fnv_tree_put_u32_le(record, offset, domain);
    offset = db_fnv_tree_put_u64_le(record, offset, total_bytes);
    offset = db_fnv_tree_put_u64_le(record, offset, leaf_count);
    offset = db_fnv_tree_put_u32_le(record, offset, depth);
    (void)db_fnv_tree_put_u64_le(record, offset, child_hash);
    return db_fnv_tree_extend(initial_hash, record, sizeof(record));
}

#ifdef __aarch64__
static uint64x2_t db_fnv1a64_multiply_neon(uint64x2_t value) {
    uint64x2_t result = value;
    result = vaddq_u64(result, vshlq_n_u64(value, DB_FNV_PRIME_SHIFT_1));
    result = vaddq_u64(result, vshlq_n_u64(value, DB_FNV_PRIME_SHIFT_4));
    result = vaddq_u64(result, vshlq_n_u64(value, DB_FNV_PRIME_SHIFT_5));
    result = vaddq_u64(result, vshlq_n_u64(value, DB_FNV_PRIME_SHIFT_7));
    result = vaddq_u64(result, vshlq_n_u64(value, DB_FNV_PRIME_SHIFT_8));
    return vaddq_u64(result, vshlq_n_u64(value, DB_FNV_PRIME_SHIFT_40));
}

static void db_fnv1a64_2x_neon(const uint8_t *data0, const uint8_t *data1,
                               size_t length, uint64_t initial0,
                               uint64_t initial1,
                               uint64_t out_hashes[DB_FNV_TREE_SSE2_LANES]) {
    const uint64_t initial[DB_FNV_TREE_SSE2_LANES] = {initial0, initial1};
    uint64x2_t hashes = vld1q_u64(initial);
    for (size_t index = 0U; index < length; index++) {
        const uint64_t input[DB_FNV_TREE_SSE2_LANES] = {data0[index],
                                                        data1[index]};
        hashes = db_fnv1a64_multiply_neon(veorq_u64(hashes, vld1q_u64(input)));
    }
    vst1q_u64(out_hashes, hashes);
}
#endif

static void db_fnv_tree_hash_equal_length(const uint8_t *const *data,
                                          size_t count, size_t length,
                                          const uint64_t *initial_hashes,
                                          uint64_t *out_hashes,
                                          int force_scalar) {
    size_t index = 0U;
#if defined(__x86_64__) || defined(__i386__)
    const int kernel = (force_scalar != 0) ? DB_HASH_X86_KERNEL_SCALAR
                                           : db_hash_select_x86_kernel();
    if (kernel == DB_HASH_X86_KERNEL_AVX2) {
        for (; index + DB_FNV_TREE_AVX2_LANES <= count;
             index += DB_FNV_TREE_AVX2_LANES) {
            db_fnv1a64_4x_avx2(data[index], data[index + 1U], data[index + 2U],
                               data[index + 3U], length, initial_hashes[index],
                               initial_hashes[index + 1U],
                               initial_hashes[index + 2U],
                               initial_hashes[index + 3U], &out_hashes[index]);
        }
    }
    if ((kernel == DB_HASH_X86_KERNEL_AVX2) ||
        (kernel == DB_HASH_X86_KERNEL_SSE2)) {
        for (; index + DB_FNV_TREE_SSE2_LANES <= count;
             index += DB_FNV_TREE_SSE2_LANES) {
            db_fnv1a64_2x_sse2(data[index], data[index + 1U], length,
                               initial_hashes[index],
                               initial_hashes[index + 1U], &out_hashes[index]);
        }
    }
#elifdef __aarch64__
    if (force_scalar == 0) {
        for (; index + DB_FNV_TREE_SSE2_LANES <= count;
             index += DB_FNV_TREE_SSE2_LANES) {
            db_fnv1a64_2x_neon(data[index], data[index + 1U], length,
                               initial_hashes[index],
                               initial_hashes[index + 1U], &out_hashes[index]);
        }
    }
#else
    (void)force_scalar;
#endif
    for (; index < count; index++) {
        out_hashes[index] =
            db_fnv_tree_extend(initial_hashes[index], data[index], length);
    }
}

static void db_fnv_tree_hash_leaves(const uint8_t *bytes, size_t total_bytes,
                                    uint32_t domain, uint64_t initial_hash,
                                    db_fnv_tree_node_t *nodes,
                                    size_t leaf_count, int force_scalar) {
    const size_t full_leaf_count = total_bytes / DB_FNV_TREE_LEAF_BYTES;
    size_t leaf_index = 0U;
    while (leaf_index < full_leaf_count) {
        const size_t remaining = full_leaf_count - leaf_index;
        const size_t batch_count = DB_MIN(remaining, DB_FNV_TREE_AVX2_LANES);
        const uint8_t *payloads[DB_FNV_TREE_AVX2_LANES] = {0};
        uint64_t initial[DB_FNV_TREE_AVX2_LANES] = {0};
        uint64_t hashes[DB_FNV_TREE_AVX2_LANES] = {0};
        for (size_t lane = 0U; lane < batch_count; lane++) {
            uint8_t header[DB_FNV_TREE_LEAF_HEADER_BYTES];
            const size_t current = leaf_index + lane;
            db_fnv_tree_encode_leaf_header(header, domain, (uint64_t)current,
                                           DB_FNV_TREE_LEAF_BYTES);
            initial[lane] =
                db_fnv_tree_extend(initial_hash, header, sizeof(header));
            payloads[lane] = bytes + (current * DB_FNV_TREE_LEAF_BYTES);
        }
        db_fnv_tree_hash_equal_length(payloads, batch_count,
                                      DB_FNV_TREE_LEAF_BYTES, initial, hashes,
                                      force_scalar);
        for (size_t lane = 0U; lane < batch_count; lane++) {
            const size_t current = leaf_index + lane;
            nodes[current] = (db_fnv_tree_node_t){
                .hash = hashes[lane],
                .first_leaf = (uint64_t)current,
                .byte_length = DB_FNV_TREE_LEAF_BYTES,
            };
        }
        leaf_index += batch_count;
    }
    if (leaf_index < leaf_count) {
        const size_t payload_length =
            total_bytes - (leaf_index * DB_FNV_TREE_LEAF_BYTES);
        uint8_t header[DB_FNV_TREE_LEAF_HEADER_BYTES];
        db_fnv_tree_encode_leaf_header(header, domain, (uint64_t)leaf_index,
                                       (uint32_t)payload_length);
        uint64_t hash =
            db_fnv_tree_extend(initial_hash, header, sizeof(header));
        hash = db_fnv_tree_extend(hash,
                                  bytes + (leaf_index * DB_FNV_TREE_LEAF_BYTES),
                                  payload_length);
        nodes[leaf_index] = (db_fnv_tree_node_t){
            .hash = hash,
            .first_leaf = (uint64_t)leaf_index,
            .byte_length = (uint64_t)payload_length,
        };
    }
}

static size_t db_fnv_tree_reduce_level(db_fnv_tree_node_t *nodes,
                                       size_t node_count, uint32_t domain,
                                       uint32_t level, uint64_t initial_hash,
                                       int force_scalar) {
    const size_t pair_count = node_count / 2U;
    size_t pair_index = 0U;
    while (pair_index < pair_count) {
        const size_t remaining = pair_count - pair_index;
        const size_t batch_count = DB_MIN(remaining, DB_FNV_TREE_AVX2_LANES);
        uint8_t records[DB_FNV_TREE_AVX2_LANES]
                       [DB_FNV_TREE_PARENT_RECORD_BYTES];
        const uint8_t *record_ptrs[DB_FNV_TREE_AVX2_LANES] = {0};
        uint64_t initial[DB_FNV_TREE_AVX2_LANES] = {0};
        uint64_t hashes[DB_FNV_TREE_AVX2_LANES] = {0};
        db_fnv_tree_node_t parents[DB_FNV_TREE_AVX2_LANES] = {0};
        for (size_t lane = 0U; lane < batch_count; lane++) {
            const size_t current_pair = pair_index + lane;
            const db_fnv_tree_node_t left = nodes[current_pair * 2U];
            const db_fnv_tree_node_t right = nodes[(current_pair * 2U) + 1U];
            db_fnv_tree_encode_parent(records[lane], domain, level, &left,
                                      &right);
            record_ptrs[lane] = records[lane];
            initial[lane] = initial_hash;
            parents[lane] = (db_fnv_tree_node_t){
                .first_leaf = left.first_leaf,
                .byte_length = left.byte_length + right.byte_length,
            };
        }
        db_fnv_tree_hash_equal_length(record_ptrs, batch_count,
                                      DB_FNV_TREE_PARENT_RECORD_BYTES, initial,
                                      hashes, force_scalar);
        for (size_t lane = 0U; lane < batch_count; lane++) {
            parents[lane].hash = hashes[lane];
            nodes[pair_index + lane] = parents[lane];
        }
        pair_index += batch_count;
    }
    if ((node_count % 2U) != 0U) {
        const db_fnv_tree_node_t child = nodes[node_count - 1U];
        uint8_t record[DB_FNV_TREE_UNARY_RECORD_BYTES];
        db_fnv_tree_encode_unary(record, domain, level, &child);
        nodes[pair_count] = (db_fnv_tree_node_t){
            .hash = db_fnv_tree_extend(initial_hash, record, sizeof(record)),
            .first_leaf = child.first_leaf,
            .byte_length = child.byte_length,
        };
    }
    return pair_count + (node_count % 2U);
}

static uint64_t db_fnv1a64_tree_internal(const void *data, size_t len_bytes,
                                         uint32_t domain, uint64_t initial_hash,
                                         int force_scalar) {
    if ((data == NULL) && (len_bytes != 0U)) {
        db_failf("hash",
                 "db_fnv1a64_tree received NULL data with len_bytes=%zu",
                 len_bytes);
    }
    if (len_bytes == 0U) {
        return db_fnv_tree_encode_root(domain, 0U, 0U, 0U, 0U, initial_hash);
    }
    const size_t leaf_count = 1U + ((len_bytes - 1U) / DB_FNV_TREE_LEAF_BYTES);
    if (leaf_count > (SIZE_MAX / sizeof(db_fnv_tree_node_t))) {
        db_failf("hash", "FNV tree node allocation overflow for %zu bytes",
                 len_bytes);
    }
    db_fnv_tree_node_t *nodes = calloc(leaf_count, sizeof(*nodes));
    if (nodes == NULL) {
        db_failf("hash", "failed to allocate %zu FNV tree nodes", leaf_count);
    }
    db_fnv_tree_hash_leaves((const uint8_t *)data, len_bytes, domain,
                            initial_hash, nodes, leaf_count, force_scalar);
    size_t node_count = leaf_count;
    uint32_t depth = 0U;
    while (node_count > 1U) {
        depth++;
        node_count = db_fnv_tree_reduce_level(nodes, node_count, domain, depth,
                                              initial_hash, force_scalar);
    }
    const uint64_t result = db_fnv_tree_encode_root(
        domain, (uint64_t)len_bytes, (uint64_t)leaf_count, depth, nodes[0].hash,
        initial_hash);
    free(nodes);
    return result;
}

uint64_t db_fnv1a64_tree_scalar(const void *data, size_t len_bytes,
                                uint32_t domain, uint64_t initial_hash) {
    return db_fnv1a64_tree_internal(data, len_bytes, domain, initial_hash, 1);
}

uint64_t db_fnv1a64_tree(const void *data, size_t len_bytes, uint32_t domain,
                         uint64_t initial_hash) {
    return db_fnv1a64_tree_internal(data, len_bytes, domain, initial_hash, 0);
}
