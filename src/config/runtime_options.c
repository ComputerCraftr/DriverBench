#include "runtime_options.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
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
    char *frame_end = NULL;
    const unsigned long frame = strtoul(text, &frame_end, 10);
    if ((frame_end == text) || (frame_end == NULL) || (*frame_end != ':') ||
        (frame > UINT32_MAX)) {
        return 0;
    }
    char *width_end = NULL;
    const unsigned long width = strtoul(frame_end + 1, &width_end, 10);
    if ((width_end == (frame_end + 1)) || (width_end == NULL) ||
        ((*width_end != 'x') && (*width_end != 'X')) || (width == 0UL) ||
        (width > INT32_MAX)) {
        return 0;
    }
    char *height_end = NULL;
    const unsigned long height = strtoul(width_end + 1, &height_end, 10);
    if ((height_end == (width_end + 1)) || (height_end == NULL) ||
        (*height_end != '\0') || (height == 0UL) || (height > INT32_MAX)) {
        return 0;
    }
    *out = (db_resize_schedule_t){
        .frame = (uint32_t)frame,
        .width = (uint32_t)width,
        .height = (uint32_t)height,
    };
    return 1;
}
