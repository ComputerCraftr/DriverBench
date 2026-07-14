#ifndef DRIVERBENCH_DB_LOG_H
#define DRIVERBENCH_DB_LOG_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DB_LOG_LEVEL_INFO = 0,
    DB_LOG_LEVEL_ERROR = 1,
} db_log_level_t;

typedef enum {
    DB_LOG_FIELD_I64 = 0,
    DB_LOG_FIELD_U64 = 1,
    DB_LOG_FIELD_DOUBLE = 2,
    DB_LOG_FIELD_BOOL = 3,
    DB_LOG_FIELD_TOKEN = 4,
    DB_LOG_FIELD_STRING = 5,
    DB_LOG_FIELD_HEX64 = 6,
} db_log_field_kind_t;

typedef struct {
    const char *key;
    db_log_field_kind_t kind;
    union {
        int64_t i64;
        uint64_t u64;
        double f64;
        int boolean;
        const char *text;
    } value;
} db_log_field_t;

typedef struct {
    const char *component;
    const char *event;
    const db_log_field_t *fields;
    size_t field_count;
} db_log_event_t;

#define DB_LOG_I64(key_, value_)                                               \
    ((db_log_field_t){.key = (key_),                                           \
                      .kind = DB_LOG_FIELD_I64,                                \
                      .value.i64 = (int64_t)(value_)})
#define DB_LOG_U64(key_, value_)                                               \
    ((db_log_field_t){.key = (key_),                                           \
                      .kind = DB_LOG_FIELD_U64,                                \
                      .value.u64 = (uint64_t)(value_)})
#define DB_LOG_DOUBLE(key_, value_)                                            \
    ((db_log_field_t){                                                         \
        .key = (key_), .kind = DB_LOG_FIELD_DOUBLE, .value.f64 = (value_)})
#define DB_LOG_BOOL(key_, value_)                                              \
    ((db_log_field_t){.key = (key_),                                           \
                      .kind = DB_LOG_FIELD_BOOL,                               \
                      .value.boolean = ((value_) != 0)})
#define DB_LOG_TOKEN(key_, value_)                                             \
    ((db_log_field_t){                                                         \
        .key = (key_), .kind = DB_LOG_FIELD_TOKEN, .value.text = (value_)})
#define DB_LOG_STRING(key_, value_)                                            \
    ((db_log_field_t){                                                         \
        .key = (key_), .kind = DB_LOG_FIELD_STRING, .value.text = (value_)})
#define DB_LOG_HEX64(key_, value_)                                             \
    ((db_log_field_t){.key = (key_),                                           \
                      .kind = DB_LOG_FIELD_HEX64,                              \
                      .value.u64 = (uint64_t)(value_)})
#define DB_LOG_FIELD_COUNT(fields_) (sizeof(fields_) / sizeof((fields_)[0]))

int db_log_format_line(char *buffer, size_t buffer_size, db_log_level_t level,
                       const db_log_event_t *event);
void db_log_emit(db_log_level_t level, const db_log_event_t *event);
void db_log_info(const char *component, const char *event,
                 const db_log_field_t *fields, size_t field_count);
void db_log_error(const char *component, const char *event,
                  const db_log_field_t *fields, size_t field_count);

static inline void db_log_renderer_capability(const char *backend_name,
                                              const char *draw_strategy,
                                              const char *geometry_storage,
                                              int replay_enabled,
                                              const char *upload_mode) {
    if ((backend_name == NULL) || (draw_strategy == NULL) ||
        (geometry_storage == NULL) || (upload_mode == NULL)) {
        return;
    }
    const db_log_field_t fields[] = {
        DB_LOG_TOKEN("draw_strategy", draw_strategy),
        DB_LOG_TOKEN("geometry_storage", geometry_storage),
        DB_LOG_BOOL("replay_enabled", replay_enabled),
        DB_LOG_TOKEN("upload_mode", upload_mode),
    };
    db_log_info(backend_name, "renderer_capability", fields,
                DB_LOG_FIELD_COUNT(fields));
}

static inline void db_log_renderer_scheduler_mode(const char *backend_name,
                                                  const char *scheduler_mode) {
    if ((backend_name == NULL) || (scheduler_mode == NULL)) {
        return;
    }
    const db_log_field_t fields[] = {DB_LOG_TOKEN("scheduler", scheduler_mode)};
    db_log_info(backend_name, "renderer_scheduler", fields,
                DB_LOG_FIELD_COUNT(fields));
}
void db_log_fail(const char *component, const char *event,
                 const db_log_field_t *fields, size_t field_count)
    __attribute__((noreturn));
void db_log_errorf_code(const char *component, const char *event,
                        const char *code, const char *format, ...)
    __attribute__((format(printf, 4, 5)));
void db_log_failf_code(const char *component, const char *event,
                       const char *code, const char *format, ...)
    __attribute__((noreturn, format(printf, 4, 5)));
void db_log_infof_code(const char *component, const char *event,
                       const char *code, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

#define DB_RUNTIME_ERROR(component_, ...)                                      \
    db_log_errorf_code((component_), "runtime_error", "operation_failed",      \
                       __VA_ARGS__)
#define DB_RUNTIME_FAIL(component_, ...)                                       \
    db_log_failf_code((component_), "runtime_error", "operation_failed",       \
                      __VA_ARGS__)
#define DB_RUNTIME_STATUS(component_, ...)                                     \
    db_log_infof_code((component_), "runtime_status", "status_update",         \
                      __VA_ARGS__)

#endif
