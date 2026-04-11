#include "core/db_core.h"
#include "displays/display_dispatch.h"
#include "displays/display_types.h"
#include "driverbench_cli.h"
#include "driverbench_config.h"

int main(int argc, char **argv) {
    db_cli_config_t cfg = {0};
    db_cli_parse_or_exit(
        db_checked_int_to_size("driverbench_main", "argc", argc),
        (const char *const *)argv, &cfg);

    if (cfg.api_is_auto != 0) {
        return db_run_display_auto(cfg.display, cfg.renderer, cfg.kms_card,
                                   &cfg);
    }

    if (cfg.api != DB_API_OPENGL) {
        cfg.renderer = DB_GL_RENDERER_GL3_3;
    }
    return db_run_display(cfg.display, cfg.api, cfg.renderer, cfg.kms_card,
                          &cfg);
}
