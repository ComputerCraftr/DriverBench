#include "renderer_opengl_gl1_5_gles1_1_ranges.h"

#include <stddef.h>

#include "../renderer_gl_common.h"

size_t db_gl1_collapse_upload_ranges_covering_span(db_gl_upload_range_t *ranges,
                                                   size_t range_count) {
    if ((ranges == NULL) || (range_count == 0U)) {
        return 0U;
    }
    size_t min_src = ranges[0].src_offset_bytes;
    size_t min_dst = ranges[0].dst_offset_bytes;
    size_t max_src_end = ranges[0].src_offset_bytes + ranges[0].size_bytes;
    size_t max_dst_end = ranges[0].dst_offset_bytes + ranges[0].size_bytes;
    for (size_t index = 1U; index < range_count; index++) {
        const db_gl_upload_range_t range = ranges[index];
        if (range.size_bytes == 0U) {
            continue;
        }
        if (range.src_offset_bytes < min_src) {
            min_src = range.src_offset_bytes;
        }
        if (range.dst_offset_bytes < min_dst) {
            min_dst = range.dst_offset_bytes;
        }
        const size_t src_end = range.src_offset_bytes + range.size_bytes;
        const size_t dst_end = range.dst_offset_bytes + range.size_bytes;
        if (src_end > max_src_end) {
            max_src_end = src_end;
        }
        if (dst_end > max_dst_end) {
            max_dst_end = dst_end;
        }
    }

    const size_t span_src = max_src_end - min_src;
    const size_t span_dst = max_dst_end - min_dst;
    if (span_src != span_dst) {
        return range_count;
    }
    ranges[0] = (db_gl_upload_range_t){
        .src_offset_bytes = min_src,
        .dst_offset_bytes = min_dst,
        .size_bytes = span_src,
    };
    return 1U;
}

size_t db_gl1_optimize_upload_ranges(db_gl_upload_range_t *ranges,
                                     size_t range_count, int allow_overdraw,
                                     size_t collapse_threshold) {
    size_t optimized_count =
        db_gl_coalesce_upload_ranges_in_place(ranges, range_count);
    if ((allow_overdraw != 0) && (optimized_count > collapse_threshold)) {
        optimized_count = db_gl1_collapse_upload_ranges_covering_span(
            ranges, optimized_count);
    }
    return optimized_count;
}
