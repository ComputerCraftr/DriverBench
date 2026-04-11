#include "../core/db_numeric.h"
#include "gl_api.h"
#include "gl_common.h"
#include "gl_proc_runtime.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int db_has_gl_extension_token(const char *exts, const char *needle) {
    if ((exts == NULL) || (needle == NULL)) {
        return 0;
    }
    const size_t needle_len = strlen(needle);
    const char *ext_ptr = exts;
    while ((ext_ptr = strstr(ext_ptr, needle)) != NULL) {
        if (((ext_ptr == exts) || (ext_ptr[-1] == ' ')) &&
            ((ext_ptr[needle_len] == '\0') || (ext_ptr[needle_len] == ' '))) {
            return 1;
        }
        ext_ptr += needle_len;
    }
    return 0;
}

int db_gl_runtime_has_extension(const db_gl_runtime_metadata_t *runtime,
                                const char *extension_name) {
    if ((runtime == NULL) || (runtime->has_valid_extensions == 0) ||
        (extension_name == NULL)) {
        return 0;
    }
    if (runtime->uses_indexed_extension_query == 0) {
        return db_has_gl_extension_token(runtime->extensions_text,
                                         extension_name);
    }
    if ((g_upload_proc_table.get_stringi == NULL) ||
        (g_upload_proc_table.get_integerv == NULL)) {
        return 0;
    }
    GLint extension_count = 0;
    g_upload_proc_table.get_integerv(GL_NUM_EXTENSIONS, &extension_count);
    for (GLint extension_index = 0; extension_index < extension_count;
         extension_index++) {
        const char *const runtime_extension =
            (const char *)g_upload_proc_table.get_stringi(
                GL_EXTENSIONS, (GLuint)extension_index);
        if ((runtime_extension != NULL) &&
            (strcmp(runtime_extension, extension_name) == 0)) {
            return 1;
        }
    }
    return 0;
}

int db_gl_runtime_has_usable_version(const db_gl_runtime_metadata_t *runtime) {
    return (runtime != NULL) && (runtime->has_valid_version != 0);
}

int db_gl_runtime_is_es_context(const db_gl_runtime_metadata_t *runtime) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           (runtime->is_es != 0);
}

static int
db_gl_runtime_is_desktop_context(const db_gl_runtime_metadata_t *runtime) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           (runtime->is_es == 0);
}

int db_gl_runtime_version_at_least(const db_gl_runtime_metadata_t *runtime,
                                   int req_major, int req_minor) {
    return (db_gl_runtime_has_usable_version(runtime) != 0) &&
           ((runtime->version_major > req_major) ||
            ((runtime->version_major == req_major) &&
             (runtime->version_minor >= req_minor)));
}

int db_gl_runtime_supports_desktop_core_or_extension(
    const db_gl_runtime_metadata_t *runtime, int req_major, int req_minor,
    const char *extension_name) {
    return (db_gl_runtime_is_desktop_context(runtime) != 0) &&
           (db_gl_runtime_version_at_least(runtime, req_major, req_minor) ||
            db_gl_runtime_has_extension(runtime, extension_name));
}

int db_gl_runtime_supports_es_core_or_extension(
    const db_gl_runtime_metadata_t *runtime, int req_major, int req_minor,
    const char *extension_name) {
    return (db_gl_runtime_is_es_context(runtime) != 0) &&
           (db_gl_runtime_version_at_least(runtime, req_major, req_minor) ||
            db_gl_runtime_has_extension(runtime, extension_name));
}

db_gl_runtime_metadata_t db_gl_runtime_metadata_load(void) {
    db_gl_runtime_metadata_t runtime = {0};
    if (g_upload_proc_table.get_string == NULL) {
        return runtime;
    }
    runtime.version_text =
        (const char *)g_upload_proc_table.get_string(GL_VERSION);
    runtime.has_valid_version = db_parse_gl_version_numbers(
        runtime.version_text, &runtime.version_major, &runtime.version_minor);
    if (runtime.has_valid_version == 0) {
        runtime.version_text = NULL;
        return runtime;
    }
    runtime.is_es = db_gl_is_es_context(runtime.version_text);
    if (runtime.is_es != 0) {
        runtime.extensions_text =
            (const char *)g_upload_proc_table.get_string(GL_EXTENSIONS);
        runtime.has_valid_extensions = DB_BOOL(runtime.extensions_text != NULL);
        return runtime;
    }
    if (db_gl_runtime_version_at_least(&runtime, 3, 0)) {
        runtime.uses_indexed_extension_query =
            DB_BOOL((g_upload_proc_table.get_stringi != NULL) &&
                    (g_upload_proc_table.get_integerv != NULL));
        runtime.has_valid_extensions = runtime.uses_indexed_extension_query;
        return runtime;
    }
    // Desktop GL before 3.0 uses the legacy GL_EXTENSIONS string query.
    if (runtime.version_major < 3) {
        runtime.extensions_text =
            (const char *)g_upload_proc_table.get_string(GL_EXTENSIONS);
        runtime.has_valid_extensions = DB_BOOL(runtime.extensions_text != NULL);
    }
    return runtime;
}

int db_gl_extensions_advertise_buffer_storage(
    const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_has_extension(runtime, "GL_EXT_buffer_storage");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 4, 4, "GL_ARB_buffer_storage");
}

int db_gl_extensions_advertise_map_buffer(
    const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_has_extension(runtime, "GL_OES_mapbuffer");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 1, 5, "GL_ARB_vertex_buffer_object");
}

int db_gl_extensions_advertise_map_buffer_range(
    const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_supports_es_core_or_extension(
            runtime, 3, 0, "GL_EXT_map_buffer_range");
    }
    return db_gl_runtime_version_at_least(runtime, 3, 0) ||
           db_gl_runtime_has_extension(runtime, "GL_ARB_map_buffer_range") ||
           db_gl_runtime_has_extension(runtime, "GL_EXT_map_buffer_range");
}

int db_gl_extensions_advertise_pbo(const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_supports_es_core_or_extension(
            runtime, 3, 0, "GL_EXT_pixel_buffer_object");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 2, 1, "GL_ARB_pixel_buffer_object");
}

int db_gl_extensions_advertise_texture_float(
    const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_supports_es_core_or_extension(
            runtime, 3, 0, "GL_OES_texture_float");
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 3, 0, "GL_ARB_texture_float");
}

int db_gl_extensions_advertise_vbo(const db_gl_runtime_metadata_t *runtime) {
    if (db_gl_runtime_has_usable_version(runtime) == 0) {
        return 0;
    }
    if (db_gl_runtime_is_es_context(runtime) != 0) {
        return db_gl_runtime_version_at_least(runtime, 1, 1);
    }
    return db_gl_runtime_supports_desktop_core_or_extension(
        runtime, 1, 5, "GL_ARB_vertex_buffer_object");
}

// 2) Proc resolver and proc table loading.
