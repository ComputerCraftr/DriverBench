#include "support/test_harness.h"

#include "displays/display_gl_runtime_common.h"
#include "displays/display_types.h"
#include "driverbench_config.h"

static void
db_test_glfw_policy_forces_full_draw_on_unstable_probe(db_test_state_t *state) {
    db_cli_config_t cfg = {0};
    db_display_gl_policy_resolution_t resolved = {0};
    const db_display_default_framebuffer_preserve_info_t preserve_info = {
        .has_probe = 1,
        .preserve_supported = 1,
        .preserve_stable = 0,
        .first_reuse_distance = 2,
    };

    db_display_resolve_opengl_display_policy(DB_GL_RENDERER_GL1_5_GLES1_1, &cfg,
                                             0, 2U, &preserve_info, &resolved);

    DB_TEST_EXPECT_EQ_U32(state, resolved.preserved_framebuffer_count, 0U);
    DB_TEST_EXPECT_TRUE(state,
                        resolved.effective_cfg.backbuffer_draw_full != 0);
    DB_TEST_EXPECT_TRUE(state, resolved.policy_reason_text != NULL);
}

static void
db_test_glfw_policy_keeps_explicit_backbuffer_mode(db_test_state_t *state) {
    db_cli_config_t cfg = {
        .backbuffer_draw_full = 0,
        .backbuffer_draw_mode_explicit = 1,
    };
    db_display_gl_policy_resolution_t resolved = {0};
    const db_display_default_framebuffer_preserve_info_t preserve_info = {
        .has_probe = 1,
        .preserve_supported = 1,
        .preserve_stable = 0,
        .first_reuse_distance = 2,
    };

    db_display_resolve_opengl_display_policy(DB_GL_RENDERER_GL1_5_GLES1_1, &cfg,
                                             0, 2U, &preserve_info, &resolved);

    DB_TEST_EXPECT_EQ_U32(state, resolved.preserved_framebuffer_count, 0U);
    DB_TEST_EXPECT_EQ_INT(state, resolved.effective_cfg.backbuffer_draw_full,
                          0);
    DB_TEST_EXPECT_TRUE(state, resolved.policy_reason_text == NULL);
}

static void
db_test_default_framebuffer_probe_policy_visibility(db_test_state_t *state) {
#ifdef __linux__
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_probe_default_framebuffer_preserve(
                            DB_GL_RENDERER_GL1_5_GLES1_1, 0) != 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_probe_default_framebuffer_preserve(
                            DB_GL_RENDERER_GL1_5_GLES1_1, 1) == 0);
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_probe_default_framebuffer_preserve(
                            DB_GL_RENDERER_GL3_3, 0) == 0);
#else
    DB_TEST_EXPECT_TRUE(state,
                        db_display_should_probe_default_framebuffer_preserve(
                            DB_GL_RENDERER_GL1_5_GLES1_1, 0) == 0);
#endif
}

static void
db_test_default_framebuffer_probe_translation(db_test_state_t *state) {
    const db_display_default_framebuffer_preserve_info_t info =
        db_display_default_framebuffer_preserve_info_make(1, 1, 0, 2);
    DB_TEST_EXPECT_EQ_INT(state, info.has_probe, 1);
    DB_TEST_EXPECT_EQ_INT(state, info.preserve_supported, 1);
    DB_TEST_EXPECT_EQ_INT(state, info.preserve_stable, 0);
    DB_TEST_EXPECT_EQ_INT(state, (int)info.first_reuse_distance, 2);
}

unsigned db_display_gl_runtime_test_run_all(void) {
    static const db_test_case_t cases[] = {
        {"glfw_policy_forces_full_draw_on_unstable_probe",
         db_test_glfw_policy_forces_full_draw_on_unstable_probe},
        {"glfw_policy_keeps_explicit_backbuffer_mode",
         db_test_glfw_policy_keeps_explicit_backbuffer_mode},
        {"default_framebuffer_probe_policy_visibility",
         db_test_default_framebuffer_probe_policy_visibility},
        {"default_framebuffer_probe_translation",
         db_test_default_framebuffer_probe_translation},
    };
    return db_test_run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}
