#include "runtime_options.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../core/db_core.h"

#define DB_RUNTIME_OPTION_CAPACITY 32U

static struct {
    const char *key;
    const char *value;
} g_runtime_options[DB_RUNTIME_OPTION_CAPACITY] = {0};

const char *db_runtime_option_get(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < DB_RUNTIME_OPTION_CAPACITY; i++) {
        if ((g_runtime_options[i].key != NULL) &&
            (strcmp(g_runtime_options[i].key, name) == 0)) {
            return g_runtime_options[i].value;
        }
    }
    return NULL;
}

void db_runtime_option_set(const char *name, const char *value) {
    if ((name == NULL) || (value == NULL)) {
        return;
    }
    for (size_t i = 0U; i < DB_RUNTIME_OPTION_CAPACITY; i++) {
        if (g_runtime_options[i].key == NULL) {
            g_runtime_options[i].key = name;
            g_runtime_options[i].value = value;
            return;
        }
        if (strcmp(g_runtime_options[i].key, name) == 0) {
            g_runtime_options[i].value = value;
            return;
        }
    }
    db_failf("runtime_options", "Runtime option capacity exceeded");
}

void db_runtime_options_reset_all(void) {
    for (size_t i = 0U; i < DB_RUNTIME_OPTION_CAPACITY; i++) {
        g_runtime_options[i].key = NULL;
        g_runtime_options[i].value = NULL;
    }
}

void db_runtime_option_set_backbuffer_draw_full(int enabled) {
    db_runtime_option_set(DB_RUNTIME_OPT_BACKBUFFER_DRAW_MODE,
                          (enabled != 0) ? "full" : "dirty");
}

void db_runtime_option_set_present_buffer_mode(const char *mode) {
    db_runtime_option_set(DB_RUNTIME_OPT_PRESENT_BUFFER_MODE,
                          (mode != NULL) ? mode : "auto");
}

int db_resize_schedule_parse(const char *text, db_resize_schedule_t *out) {
    if ((text == NULL) || (out == NULL) || (text[0] == '\0')) {
        return 0;
    }
    const char *frame_end = NULL;
    uint32_t frame = 0U;
    if ((db_parse_u32_prefix(text, DB_PARSE_BASE_DECIMAL, &frame, &frame_end) ==
         0) ||
        (*frame_end != ':')) {
        return 0;
    }
    const char *width_end = NULL;
    uint32_t width = 0U;
    if ((db_parse_u32_prefix(frame_end + 1, DB_PARSE_BASE_DECIMAL, &width,
                             &width_end) == 0) ||
        ((*width_end != 'x') && (*width_end != 'X')) || (width == 0U) ||
        (width > INT32_MAX)) {
        return 0;
    }
    const char *height_end = NULL;
    uint32_t height = 0U;
    if ((db_parse_u32_prefix(width_end + 1, DB_PARSE_BASE_DECIMAL, &height,
                             &height_end) == 0) ||
        (*height_end != '\0') || (height == 0U) || (height > INT32_MAX)) {
        return 0;
    }
    *out = (db_resize_schedule_t){
        .frame = frame,
        .width = width,
        .height = height,
    };
    return 1;
}
