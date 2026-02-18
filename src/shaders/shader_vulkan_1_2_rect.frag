#version 450
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform PC {
    vec2 offsetNDC;
    vec2 scaleNDC;
    vec4 color;
    ivec4 renderParams;   // x=mode, y=gridRows, z=headRow, w=clearingPhase
    ivec4 gradientParams; // x=sweepWindowRows, y=fillWindowRows, z=viewportHeight, w=gridCols
    ivec4 snakeParams;    // x=activeCursor, y=batchSize, z=phaseCompleted, w=viewportWidth
    vec4 baseColor;
    vec4 targetColor;
} pc;
#else
layout(std140, binding = 0) uniform PC {
    vec2 offsetNDC;
    vec2 scaleNDC;
    vec4 color;
    ivec4 renderParams;
    ivec4 gradientParams;
    ivec4 snakeParams;
    vec4 baseColor;
    vec4 targetColor;
} pc;
#endif

const int RENDER_MODE_GRADIENT_SWEEP = 0;
const int RENDER_MODE_BANDS = 1;
const int RENDER_MODE_SNAKE_GRID = 2;
const int RENDER_MODE_GRADIENT_FILL = 3;

int db_row_from_frag_coord() {
    float rows = float(max(pc.renderParams.y, 1));
    float viewportHeight = float(max(pc.gradientParams.z, 1));
    float y = clamp(gl_FragCoord.y, 0.0, viewportHeight - 1.0);
    return int(floor((y * rows) / viewportHeight));
}

vec4 db_gradient_sweep_color() {
    int rows_i = max(pc.renderParams.y, 1);
    int head_i = pc.renderParams.z % rows_i;
    if(head_i < 0) {
        head_i += rows_i;
    }
    int row_i = pc.snakeParams.x % rows_i;
    if(row_i < 0) {
        row_i += rows_i;
    }
    int window_i = clamp(pc.gradientParams.x, 1, rows_i);
    int delta_i = (row_i - head_i + rows_i) % rows_i;
    if(delta_i >= window_i) {
        return pc.targetColor;
    }
    float halfSpan = (float(window_i) - 1.0) * 0.5;
    float blend = 0.0;
    if(halfSpan > 0.0) {
        blend = abs(float(delta_i) - halfSpan) / halfSpan;
    }
    return mix(pc.baseColor, pc.targetColor, blend);
}

vec4 db_gradient_fill_color(int row_i) {
    int rows_i = max(pc.renderParams.y, 1);
    int head_i = pc.renderParams.z % rows_i;
    if(head_i < 0) {
        head_i += rows_i;
    }
    int clearingPhase = pc.renderParams.w;
    vec4 sourceColor = (clearingPhase != 0) ? pc.targetColor : pc.baseColor;
    vec4 targetColor = (clearingPhase != 0) ? pc.baseColor : pc.targetColor;
    if(row_i >= head_i) {
        return sourceColor;
    }
    int delta_i = head_i - row_i;
    int window_i = clamp(pc.gradientParams.y, 1, rows_i);
    if(delta_i >= window_i) {
        return targetColor;
    }
    float blend = float(delta_i) / float(window_i);
    return mix(sourceColor, targetColor, blend);
}

int db_col_from_frag_coord() {
    float cols = float(max(pc.gradientParams.w, 1));
    float viewportWidth = float(max(pc.snakeParams.w, 1));
    float x = clamp(gl_FragCoord.x, 0.0, viewportWidth - 1.0);
    return int(floor((x * cols) / viewportWidth));
}

vec4 db_snake_grid_color(int row_i, int col_i) {
    int cols = max(pc.gradientParams.w, 1);
    int step = row_i * cols;
    if((row_i & 1) == 0) {
        step += col_i;
    } else {
        step += (cols - 1 - col_i);
    }

    int activeCursor = max(pc.snakeParams.x, 0);
    int batchSize = max(pc.snakeParams.y, 1);
    int phaseCompleted = pc.snakeParams.z;
    int clearingPhase = pc.renderParams.w;

    vec4 sourceColor = (clearingPhase != 0) ? pc.targetColor : pc.baseColor;
    vec4 targetColor = (clearingPhase != 0) ? pc.baseColor : pc.targetColor;
    if(phaseCompleted != 0) {
        return targetColor;
    }
    if(step < activeCursor) {
        return targetColor;
    }
    if(step >= (activeCursor + batchSize)) {
        return sourceColor;
    }

    int windowIndex = step - activeCursor;
    float blend = float(batchSize - windowIndex) / float(batchSize);
    return mix(sourceColor, targetColor, blend);
}

void main() {
    int renderMode = pc.renderParams.x;
    if(renderMode == RENDER_MODE_GRADIENT_SWEEP) {
        outColor = db_gradient_sweep_color();
        return;
    }
    if(renderMode == RENDER_MODE_GRADIENT_FILL) {
        outColor = db_gradient_fill_color(db_row_from_frag_coord());
        return;
    }
    if(renderMode == RENDER_MODE_SNAKE_GRID) {
        outColor = db_snake_grid_color(db_row_from_frag_coord(), db_col_from_frag_coord());
        return;
    }
    outColor = vColor;
}
