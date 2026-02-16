#include "renderer_opengl_gl1_5_gles1_1.h"

#include <math.h>
#include <stdint.h>

#include "../../displays/bench_config.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

void db_renderer_opengl_gl1_5_gles1_1_init(void) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

void db_renderer_opengl_gl1_5_gles1_1_render_frame(double time_s) {
    glBegin(GL_TRIANGLES);
    for (uint32_t band = 0; band < BENCH_BANDS; band++) {
        float x0 = ((2.0F * (float)band) / (float)BENCH_BANDS) - 1.0F;
        float x1 = ((2.0F * (float)(band + 1U)) / (float)BENCH_BANDS) - 1.0F;

        float pulse = BENCH_PULSE_BASE_F +
                      (BENCH_PULSE_AMP_F *
                       sinf((float)((time_s * BENCH_PULSE_FREQ_F) +
                                    ((float)band * BENCH_PULSE_PHASE_F))));
        float color_r =
            pulse * (BENCH_COLOR_R_BASE_F +
                     BENCH_COLOR_R_SCALE_F * (float)band / (float)BENCH_BANDS);
        float color_g = pulse * BENCH_COLOR_G_SCALE_F;
        float color_b = 1.0F - color_r;

        glColor3f(color_r, color_g, color_b);
        glVertex2f(x0, -1.0F);
        glVertex2f(x1, -1.0F);
        glVertex2f(x1, 1.0F);
        glVertex2f(x0, -1.0F);
        glVertex2f(x1, 1.0F);
        glVertex2f(x0, 1.0F);
    }
    glEnd();
}

void db_renderer_opengl_gl1_5_gles1_1_shutdown(void) {}
