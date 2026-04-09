#include "driverbench_cli.h"
#include "driverbench_config.h"

#include <stdio.h>
#include <stdlib.h>

#include "cli/cli_parse.h"
#include "core/db_core.h"
#include "displays/display_dispatch.h"
#include "displays/display_types.h"

enum { DB_DRIVERBENCH_CLI_ERROR_TEXT_SIZE = 512 };

static void db_usage(void) {
    const char *renderer_usage = "auto|gl1_5_gles1_1|gl3_3";
    char offscreen_caps[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
    char glfw_caps[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
    char kms_caps[DB_DISPLAY_CAPABILITY_TEXT_MAX] = {0};
    (void)db_dispatch_format_display_capabilities(
        DB_DISPLAY_OFFSCREEN, offscreen_caps, sizeof(offscreen_caps));
    (void)db_dispatch_format_display_capabilities(DB_DISPLAY_GLFW_WINDOW,
                                                  glfw_caps, sizeof(glfw_caps));
    (void)db_dispatch_format_display_capabilities(DB_DISPLAY_LINUX_KMS_ATOMIC,
                                                  kms_caps, sizeof(kms_caps));
    fputs("Usage: driverbench [dispatch options] [runtime options]\n"
          "\nDispatch options:\n"
          "  --api <auto|cpu|opengl|vulkan>\n"
          "  --renderer <",
          stderr);
    fputs(renderer_usage, stderr);
    fputs(">\n"
          "  --display <offscreen|glfw_window|linux_kms_atomic>  (required)\n"
          "  --renderer requires --api opengl when explicitly set\n"
          "  --kms-card <path>\n"
          "\nRuntime options:\n"
          "  --allow-remote-display <0|1>\n"
          "  --backbuffer-draw-mode <dirty|full>\n"
          "  --benchmark-mode "
          "<gradient_sweep|bands|snake_grid|gradient_fill|snake_rect|snake_"
          "shapes>\n"
          "  --bench-speed <value>\n"
          "  --debug-clear-default-framebuffer <0|1>\n"
          "  --cpu-hdr <0|1>\n"
          "  --fps-cap <value>\n"
          "  --hash <none|state|pixel|both>\n"
          "  --frame-limit <value>\n"
          "  --glfw-hidden-window <0|1>\n"
          "  --hash-report <final|aggregate|both>\n"
          "  --metrics-mode <basic|dual>\n"
          "  --present-buffer-mode <auto|replace|single_source|ring>\n"
          "  --random-seed <value>\n"
          "  --vk-allow-cpu-workers <0|1>\n"
          "  --vk-multi-device-policy <auto|group_only|independent_ok>\n"
          "  --vk-no-present <0|1>\n"
          "  --vsync <0|1|on|off|true|false>\n"
          "  --help\n"
          "\nDisplay capability summary:\n"
          "  offscreen: ",
          stderr);
    fputs(offscreen_caps, stderr);
    fputs("\n  glfw_window: ", stderr);
    fputs(glfw_caps, stderr);
    fputs("\n  linux_kms_atomic: ", stderr);
    fputs(kms_caps, stderr);
    fputs("\n", stderr);
}

void db_cli_parse_or_exit(int argc, char **argv, db_cli_config_t *out_cfg) {
    char error[DB_DRIVERBENCH_CLI_ERROR_TEXT_SIZE] = {0};
    int show_help = 0;
    int print_usage = 0;
    if (db_cli_try_parse(argc, argv, out_cfg, &show_help, &print_usage, error,
                         sizeof(error)) != 0) {
        if (show_help != 0) {
            db_usage();
            exit(EXIT_SUCCESS);
        }
        return;
    }
    if (print_usage != 0) {
        db_usage();
    }
    db_failf("driverbench_cli", "%s",
             (error[0] != '\0') ? error : "CLI parse failed");
}
