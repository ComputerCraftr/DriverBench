#include "db_log.h"

#include "db_core.h"

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DB_LOG_LINE_CAPACITY = 8192U,
    DB_LOG_DETAIL_CAPACITY = 2048U,
};

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    int valid;
} db_log_writer_t;

static void db_log_append_char(db_log_writer_t *writer, char value) {
    if ((writer == NULL) || (writer->valid == 0)) {
        return;
    }
    if ((writer->length + 1U) >= writer->capacity) {
        writer->valid = 0;
        return;
    }
    writer->buffer[writer->length++] = value;
    writer->buffer[writer->length] = '\0';
}

static void db_log_append_text(db_log_writer_t *writer, const char *value) {
    if ((writer == NULL) || (value == NULL) || (writer->valid == 0)) {
        if (writer != NULL) {
            writer->valid = 0;
        }
        return;
    }
    while ((writer->valid != 0) && (*value != '\0')) {
        db_log_append_char(writer, *value++);
    }
}

static void db_log_append_format(db_log_writer_t *writer, const char *format,
                                 ...) __attribute__((format(printf, 2, 3)));
static void db_log_append_format(db_log_writer_t *writer, const char *format,
                                 ...) {
    if ((writer == NULL) || (format == NULL) || (writer->valid == 0) ||
        (writer->length >= writer->capacity)) {
        return;
    }
    va_list args;
    va_start(args, format);
    const int written =
        db_vsnprintf(writer->buffer + writer->length,
                     writer->capacity - writer->length, format, args);
    va_end(args);
    if ((written < 0) ||
        ((size_t)written >= (writer->capacity - writer->length))) {
        writer->valid = 0;
        return;
    }
    writer->length += (size_t)written;
}

static int db_log_identifier_valid(const char *value) {
    if ((value == NULL) || (value[0] == '\0') ||
        !((value[0] >= 'a') && (value[0] <= 'z'))) {
        return 0;
    }
    size_t length = 1U;
    for (const char *cursor = value + 1; *cursor != '\0'; cursor++) {
        if (length >= DB_LOG_IDENTIFIER_CAPACITY) {
            return 0;
        }
        if (!(((*cursor >= 'a') && (*cursor <= 'z')) ||
              ((*cursor >= '0') && (*cursor <= '9')) || (*cursor == '_'))) {
            return 0;
        }
        length++;
    }
    return 1;
}

static int db_log_component_valid(const char *value) {
    if ((value == NULL) || (value[0] == '\0')) {
        return 0;
    }
    size_t length = 0U;
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (length >= DB_LOG_IDENTIFIER_CAPACITY) {
            return 0;
        }
        if (!(isalnum((unsigned char)*cursor) || (*cursor == '_') ||
              (*cursor == '-') || (*cursor == '.'))) {
            return 0;
        }
        length++;
    }
    return 1;
}

static int db_log_token_valid(const char *value) {
    if ((value == NULL) || (value[0] == '\0')) {
        return 0;
    }
    size_t length = 0U;
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (length >= DB_LOG_TOKEN_CAPACITY) {
            return 0;
        }
        if (!(isalnum((unsigned char)*cursor) || (*cursor == '_') ||
              (*cursor == '-') || (*cursor == '.') || (*cursor == ':') ||
              (*cursor == '/') || (*cursor == '+'))) {
            return 0;
        }
        length++;
    }
    return 1;
}

static int db_log_fields_valid(const db_log_event_t *event) {
    if (event == NULL) {
        return 0;
    }
    if (event->field_count == 0U) {
        return 1;
    }
    if ((event->fields == NULL) ||
        (event->field_count > DB_LOG_FIELD_CAPACITY)) {
        return 0;
    }
    for (size_t i = 0U; i < event->field_count; i++) {
        const db_log_field_t *const field = &event->fields[i];
        if (!db_log_identifier_valid(field->key) ||
            (strcmp(field->key, "event") == 0) ||
            (strcmp(field->key, "schema") == 0)) {
            return 0;
        }
        for (size_t prior = 0U; prior < i; prior++) {
            if (strcmp(field->key, event->fields[prior].key) == 0) {
                return 0;
            }
        }
        if ((field->kind == DB_LOG_FIELD_DOUBLE) &&
            !isfinite(field->value.f64)) {
            return 0;
        }
        if ((field->kind == DB_LOG_FIELD_TOKEN) &&
            !db_log_token_valid(field->value.text)) {
            return 0;
        }
        if ((field->kind == DB_LOG_FIELD_STRING) &&
            (field->value.text == NULL)) {
            return 0;
        }
        if ((field->kind < DB_LOG_FIELD_I64) ||
            (field->kind > DB_LOG_FIELD_HEX64)) {
            return 0;
        }
    }
    return 1;
}

static void db_log_append_string(db_log_writer_t *writer, const char *value) {
    static const char hex[] = "0123456789abcdef";
    db_log_append_char(writer, '"');
    for (const unsigned char *cursor = (const unsigned char *)value;
         (writer->valid != 0) && (*cursor != '\0'); cursor++) {
        switch (*cursor) {
        case '"':
            db_log_append_text(writer, "\\\"");
            break;
        case '\\':
            db_log_append_text(writer, "\\\\");
            break;
        case '\n':
            db_log_append_text(writer, "\\n");
            break;
        case '\r':
            db_log_append_text(writer, "\\r");
            break;
        case '\t':
            db_log_append_text(writer, "\\t");
            break;
        default:
            if (*cursor < 0x20U) {
                db_log_append_text(writer, "\\u00");
                db_log_append_char(writer, hex[*cursor >> 4U]);
                db_log_append_char(writer, hex[*cursor & UINT8_C(0x0F)]);
            } else {
                db_log_append_char(writer, (char)*cursor);
            }
            break;
        }
    }
    db_log_append_char(writer, '"');
}

static void db_log_append_field(db_log_writer_t *writer,
                                const db_log_field_t *field) {
    db_log_append_char(writer, ' ');
    db_log_append_text(writer, field->key);
    db_log_append_char(writer, '=');
    switch (field->kind) {
    case DB_LOG_FIELD_I64:
        // inttypes.h is the public provider; macOS attributes these format
        // macros to private sys/_types headers.
        // NOLINTNEXTLINE(misc-include-cleaner)
        db_log_append_format(writer, "%" PRId64, field->value.i64);
        break;
    case DB_LOG_FIELD_U64:
        // NOLINTNEXTLINE(misc-include-cleaner)
        db_log_append_format(writer, "%" PRIu64, field->value.u64);
        break;
    case DB_LOG_FIELD_DOUBLE:
        db_log_append_format(writer, "%.9g", field->value.f64);
        break;
    case DB_LOG_FIELD_BOOL:
        db_log_append_text(writer, field->value.boolean ? "true" : "false");
        break;
    case DB_LOG_FIELD_TOKEN:
        db_log_append_text(writer, field->value.text);
        break;
    case DB_LOG_FIELD_STRING:
        db_log_append_string(writer, field->value.text);
        break;
    case DB_LOG_FIELD_HEX64:
        // NOLINTNEXTLINE(misc-include-cleaner)
        db_log_append_format(writer, "0x%016" PRIx64, field->value.u64);
        break;
    }
}

int db_log_format_line(char *buffer, size_t buffer_size, db_log_level_t level,
                       const db_log_event_t *event) {
    if ((buffer == NULL) || (buffer_size == 0U) || (event == NULL) ||
        !db_log_component_valid(event->component) ||
        !db_log_identifier_valid(event->event) || !db_log_fields_valid(event) ||
        ((level != DB_LOG_LEVEL_INFO) && (level != DB_LOG_LEVEL_ERROR))) {
        return 0;
    }
    db_log_writer_t writer = {
        .buffer = buffer,
        .capacity = buffer_size,
        .length = 0U,
        .valid = 1,
    };
    buffer[0] = '\0';
    db_log_append_char(&writer, '[');
    db_log_append_text(&writer, event->component);
    db_log_append_text(&writer, (level == DB_LOG_LEVEL_INFO)
                                    ? "][info] event="
                                    : "][error] event=");
    db_log_append_text(&writer, event->event);
    db_log_append_text(&writer, " schema=2");
    for (size_t i = 0U; i < event->field_count; i++) {
        db_log_append_field(&writer, &event->fields[i]);
    }
    db_log_append_char(&writer, '\n');
    if (writer.valid == 0) {
        return 0;
    }
    return db_checked_size_to_int("db_log", "formatted_line_length",
                                  writer.length);
}

void db_log_emit(db_log_level_t level, const db_log_event_t *event) {
    char line[DB_LOG_LINE_CAPACITY] = {0};
    FILE *const stream = (level == DB_LOG_LEVEL_INFO) ? stdout : stderr;
    if (db_log_format_line(line, sizeof(line), level, event) <= 0) {
        fputs("[db_log][error] event=log_contract_error schema=2\n", stderr);
        return;
    }
    fputs(line, stream);
    fflush(stream);
}

void db_log_info(const char *component, const char *event,
                 const db_log_field_t *fields, size_t field_count) {
    db_log_emit(DB_LOG_LEVEL_INFO,
                &(const db_log_event_t){component, event, fields, field_count});
}

void db_log_error(const char *component, const char *event,
                  const db_log_field_t *fields, size_t field_count) {
    db_log_emit(DB_LOG_LEVEL_ERROR,
                &(const db_log_event_t){component, event, fields, field_count});
}

void db_log_fail(const char *component, const char *event,
                 const db_log_field_t *fields, size_t field_count) {
    db_log_error(component, event, fields, field_count);
    exit(EXIT_FAILURE);
}

static void db_log_emit_code_detail(db_log_level_t level, const char *component,
                                    const char *event, const char *code,
                                    const char *format, va_list args)
    __attribute__((format(printf, 5, 0)));

static void db_log_emit_code_detail(db_log_level_t level, const char *component,
                                    const char *event, const char *code,
                                    const char *format, va_list args) {
    char detail[DB_LOG_DETAIL_CAPACITY] = {0};
    const int format_result =
        db_vsnprintf(detail, sizeof(detail), format, args);
    if (format_result < 0) {
        detail[0] = '\0';
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("code", code),
        DB_LOG_STRING("detail", detail),
    };
    db_log_emit(level, &(const db_log_event_t){component, event, fields,
                                               DB_LOG_FIELD_COUNT(fields)});
}

void db_log_errorf_code(const char *component, const char *event,
                        const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    db_log_emit_code_detail(DB_LOG_LEVEL_ERROR, component, event, code, format,
                            args);
    va_end(args);
}

void db_log_infof_code(const char *component, const char *event,
                       const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    db_log_emit_code_detail(DB_LOG_LEVEL_INFO, component, event, code, format,
                            args);
    va_end(args);
}

void db_log_failf_code(const char *component, const char *event,
                       const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    db_log_emit_code_detail(DB_LOG_LEVEL_ERROR, component, event, code, format,
                            args);
    va_end(args);
    exit(EXIT_FAILURE);
}
