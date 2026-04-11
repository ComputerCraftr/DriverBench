#include "core/db_format_contract.h"
#include "gl_common.h"

#include "../core/db_log.h"
#include "../core/db_trace.h"

#include <stddef.h>

static const char *trace_label_or(const char *label, const char *fallback) {
    return (label != NULL) ? label : fallback;
}

void db_gl_error_trace_dump(const db_gl_error_trace_t *trace) {
    if ((trace == NULL) || (trace->count == 0U)) {
        return;
    }
    const db_log_field_t summary_fields[] = {
        DB_LOG_U64("count", trace->count),
    };
    db_log_error("gl_error_trace", "gl_error_summary", summary_fields,
                 DB_LOG_FIELD_COUNT(summary_fields));
    for (size_t i = 0U; i < trace->count; i++) {
        const db_gl_error_record_t *rec = &trace->records[i];
        const db_log_field_t fields[] = {
            DB_LOG_U64("index", i),
            DB_LOG_HEX64("error", rec->error_code),
            DB_LOG_TOKEN("phase", (rec->phase != NULL) ? rec->phase : "none"),
            DB_LOG_TOKEN("target",
                         (rec->target != NULL) ? rec->target : "none"),
            DB_LOG_TOKEN("context",
                         (rec->context != NULL) ? rec->context : "none"),
        };
        db_log_error("gl_error_trace", "gl_error", fields,
                     DB_LOG_FIELD_COUNT(fields));
    }
}

void db_gl_shadow_upload_trace_dump(const db_gl_shadow_upload_trace_t *trace) {
    if (trace == NULL) {
        return;
    }
    const int trace_level = db_trace_config_current().shadow_upload;
    if (trace_level == 0) {
        db_gl_error_trace_dump(&trace->error_trace);
        return;
    }
    const size_t emitted_count =
        ((trace_level >= 3) || (trace->upload_span_count < 128U))
            ? trace->upload_span_count
            : 128U;
    const db_log_field_t upload_fields[] = {
        DB_LOG_BOOL("attempted", trace->full_upload_attempted),
        DB_LOG_BOOL("executed", trace->full_upload_executed),
        DB_LOG_U64("slot", trace->slot_index),
        DB_LOG_U64("bytes", trace->total_bytes),
        DB_LOG_TOKEN("target_mode",
                     trace_label_or(trace->target_mode_label, "none")),
        DB_LOG_TOKEN("upload_mode",
                     trace_label_or(trace->upload_mode_label, "none")),
        DB_LOG_TOKEN("effective_upload_mode",
                     trace_label_or(trace->executed_upload_mode_label, "none")),
        DB_LOG_TOKEN("src", trace_label_or(trace->source_label, "none")),
        DB_LOG_TOKEN("fallback",
                     trace_label_or(trace->fallback_mode_label, "none")),
        DB_LOG_BOOL("seeded", trace->seeded_shadow_ring),
        DB_LOG_TOKEN("seed_src",
                     trace_label_or(trace->seed_source_label, "none")),
        DB_LOG_TOKEN("history_src",
                     trace_label_or(trace->history_source_label, "none")),
        DB_LOG_U64("required_previous_frames", trace->required_previous_frames),
        DB_LOG_U64("historical_blocks", trace->historical_block_count),
        DB_LOG_U64("repair_blocks", trace->repair_block_count),
        DB_LOG_U64("pixel_width", trace->pixel_width),
        DB_LOG_U64("pixel_height", trace->pixel_height),
        DB_LOG_TOKEN("format", (trace->pixel_payload.format ==
                                DB_GL_SHADOW_PRESENT_TEXTURE_RGBA16F)
                                   ? "rgba16f"
                                   : "rgba8"),
        DB_LOG_U64("stride", trace->pixel_payload.row_stride_bytes),
        DB_LOG_U64("payload_bytes", trace->pixel_payload.total_bytes),
        DB_LOG_U64("span_count", trace->upload_span_count),
        DB_LOG_U64("emitted_count", (trace_level >= 2) ? emitted_count : 0U),
        DB_LOG_U64("omitted_count",
                   (trace_level >= 2) ? trace->upload_span_count - emitted_count
                                      : trace->upload_span_count),
    };
    db_log_info("shadow_trace", "shadow_upload", upload_fields,
                DB_LOG_FIELD_COUNT(upload_fields));
    if (trace_level >= 2) {
        for (size_t i = 0U; i < emitted_count; i++) {
            const db_gl_upload_span_trace_t *span = &trace->upload_spans[i];
            const db_log_field_t span_fields[] = {
                DB_LOG_U64("index", i),
                DB_LOG_U64("x", span->block.col_start),
                DB_LOG_U64("y", span->block.row_start),
                DB_LOG_U64("width", span->block.col_count),
                DB_LOG_U64("height", span->block.row_count),
                DB_LOG_U64("offset", span->offset_bytes),
                DB_LOG_U64("bytes", span->size_bytes),
                DB_LOG_TOKEN("src", trace_label_or(span->source_label, "none")),
            };
            db_log_info("shadow_trace", "shadow_upload_span", span_fields,
                        DB_LOG_FIELD_COUNT(span_fields));
        }
    }
    db_gl_error_trace_dump(&trace->error_trace);
}
