#version 450
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uHistoryTex;

#if defined(VULKAN) || defined(GL_KHR_vulkan_glsl)
layout(push_constant) uniform PC {
    vec2 offsetNDC;
    vec2 scaleNDC;
    vec4 color;
    ivec4 renderParams;   // x=mode, y=gridRows, z=headRow, w=clearingPhase
    ivec4 gradientParams; // x=sweepWindowRows, y=fillWindowRows,
                          // z=viewportHeight, w=gridCols
    ivec4 snakeParams;    // x=activeCursor, y=batchSize, z=phaseCompleted,
                          // w=viewportWidth
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
const int RENDER_MODE_RECT_SNAKE = 4;

struct RectSnakeDesc {
    int x;
    int y;
    int w;
    int h;
    vec3 color;
};

vec4 db_rgba(vec3 color_rgb) {
    return vec4(color_rgb, 1.0);
}

vec3 db_target_color_for_phase(int clearingPhase) {
    return (clearingPhase != 0) ? pc.baseColor.rgb : pc.targetColor.rgb;
}

float db_window_blend(int batchSize, int windowIndex) {
    if(batchSize <= 1) {
        return 1.0;
    }
    return float((batchSize - 1) - windowIndex) / float(batchSize - 1);
}

vec3 db_blend_prior_to_target(
    vec3 prior_color,
    vec3 target_color,
    float blend
) {
    return mix(prior_color, target_color, blend);
}

int db_row_from_frag_coord() {
    float rows = float(max(pc.renderParams.y, 1));
    float viewportHeight = float(max(pc.gradientParams.z, 1));
    float y = clamp(gl_FragCoord.y, 0.0, viewportHeight - 1.0);
    return int(floor((y * rows) / viewportHeight));
}

vec4 db_gradient_sweep_color(int row_i) {
    int rows_i = max(pc.renderParams.y, 1);
    int head_i = max(pc.renderParams.z, 0);
    int window_i = clamp(pc.gradientParams.x, 1, rows_i);
    if((head_i > row_i) || ((row_i - head_i) >= window_i)) {
        return pc.targetColor;
    }
    int delta_i = row_i - head_i;
    float halfSpan = (float(window_i) - 1.0) * 0.5;
    float blend = 0.0;
    if(halfSpan > 0.0) {
        blend = abs(float(delta_i) - halfSpan) / halfSpan;
    }
    return mix(pc.baseColor, pc.targetColor, blend);
}

vec4 db_gradient_fill_color(int row_i) {
    int rows_i = max(pc.renderParams.y, 1);
    int head_i = max(pc.renderParams.z, 0);
    int clearingPhase = pc.renderParams.w;
    vec4 sourceColor = (clearingPhase != 0) ? pc.targetColor : pc.baseColor;
    vec3 targetColor = db_target_color_for_phase(clearingPhase);
    if(row_i >= head_i) {
        return sourceColor;
    }
    int delta_i = head_i - row_i;
    int window_i = clamp(pc.gradientParams.y, 1, rows_i);
    if(delta_i >= window_i) {
        return db_rgba(targetColor);
    }
    if(window_i <= 1) {
        return db_rgba(targetColor);
    }
    float blend = float(delta_i) / float(window_i - 1);
    return mix(sourceColor, vec4(targetColor, 1.0), blend);
}

int db_col_from_frag_coord() {
    float cols = float(max(pc.gradientParams.w, 1));
    float viewportWidth = float(max(pc.snakeParams.w, 1));
    float x = clamp(gl_FragCoord.x, 0.0, viewportWidth - 1.0);
    return int(floor((x * cols) / viewportWidth));
}

vec4 db_snake_grid_color(int row_i, int col_i, vec3 prior_color) {
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

    vec3 targetColor = db_target_color_for_phase(clearingPhase);
    if(phaseCompleted != 0) {
        return db_rgba(targetColor);
    }
    if(step < activeCursor) {
        return db_rgba(targetColor);
    }
    if(step >= (activeCursor + batchSize)) {
        return db_rgba(prior_color);
    }

    int windowIndex = step - activeCursor;
    float blend = db_window_blend(batchSize, windowIndex);
    return db_rgba(db_blend_prior_to_target(prior_color, targetColor, blend));
}

uint db_mix_u32(uint value) {
    value ^= (value >> 16u);
    value *= 0x7FEB352Du;
    value ^= (value >> 15u);
    value *= 0x846CA68Bu;
    value ^= (value >> 16u);
    return value;
}

int db_u32_range(uint seed, int min_v, int max_v) {
    if(max_v <= min_v) {
        return min_v;
    }
    uint span = uint((max_v - min_v) + 1);
    return min_v + int(seed % span);
}

float db_rect_channel(uint seed) {
    return 0.25 + ((float(seed & 255u) / 255.0) * 0.70);
}

RectSnakeDesc db_rect_snake_desc(
    int rect_seed,
    uint rect_index,
    int rows_i,
    int cols_i
) {
    RectSnakeDesc rect;
    uint seed_base = db_mix_u32(uint(max(rect_seed, 0)) + (rect_index * 0x85EBCA77u) + 1u);
    int min_w = (cols_i >= 16) ? 8 : 1;
    int min_h = (rows_i >= 16) ? 8 : 1;
    int max_w = (cols_i >= min_w) ? ((cols_i / 3) + min_w) : min_w;
    int max_h = (rows_i >= min_h) ? ((rows_i / 3) + min_h) : min_h;
    rect.w = db_u32_range(db_mix_u32(seed_base ^ 0xA511E9B3u), min_w, min(max_w, cols_i));
    rect.h = db_u32_range(db_mix_u32(seed_base ^ 0x63D83595u), min_h, min(max_h, rows_i));
    int max_x = (cols_i > rect.w) ? (cols_i - rect.w) : 0;
    int max_y = (rows_i > rect.h) ? (rows_i - rect.h) : 0;
    rect.x = db_u32_range(db_mix_u32(seed_base ^ 0x9E3779B9u), 0, max_x);
    rect.y = db_u32_range(db_mix_u32(seed_base ^ 0xC2B2AE35u), 0, max_y);
    rect.color.r = db_rect_channel(db_mix_u32(seed_base ^ 0x27D4EB2Fu));
    rect.color.g = db_rect_channel(db_mix_u32(seed_base ^ 0x165667B1u));
    rect.color.b = db_rect_channel(db_mix_u32(seed_base ^ 0x85EBCA77u));
    return rect;
}

bool db_rect_contains(RectSnakeDesc rect, int row_i, int col_i) {
    return (row_i >= rect.y) && (row_i < (rect.y + rect.h)) &&
        (col_i >= rect.x) && (col_i < (rect.x + rect.w));
}

int db_rect_snake_step(RectSnakeDesc rect, int row_i, int col_i) {
    int local_row = row_i - rect.y;
    int local_col = col_i - rect.x;
    int snake_col = ((local_row & 1) == 0) ? local_col : ((rect.w - 1) - local_col);
    return (local_row * rect.w) + snake_col;
}

vec4 db_rect_snake_color(int row_i, int col_i, vec3 prior_color) {
    int rows_i = max(pc.renderParams.y, 1);
    int cols_i = max(pc.gradientParams.w, 1);
    int rect_seed = max(pc.renderParams.w, 0);
    uint rect_index_u = uint(max(pc.renderParams.z, 0));
    RectSnakeDesc current_rect = db_rect_snake_desc(rect_seed, rect_index_u, rows_i, cols_i);
    if(!db_rect_contains(current_rect, row_i, col_i)) {
        return db_rgba(prior_color);
    }
    int step = db_rect_snake_step(current_rect, row_i, col_i);
    int cursor = max(pc.snakeParams.x, 0);
    int batch_size = max(pc.snakeParams.y, 1);
    if(step < cursor) {
        return db_rgba(current_rect.color);
    }
    if(step < (cursor + batch_size)) {
        int window_index = step - cursor;
        float blend = db_window_blend(batch_size, window_index);
        return db_rgba(db_blend_prior_to_target(prior_color, current_rect.color, blend));
    }
    return db_rgba(prior_color);
}

void main() {
    int renderMode = pc.renderParams.x;
    if(renderMode == RENDER_MODE_BANDS) {
        outColor = vColor;
        return;
    }
    int row_i = db_row_from_frag_coord();
    if(renderMode == RENDER_MODE_GRADIENT_SWEEP) {
        outColor = db_gradient_sweep_color(pc.snakeParams.x);
        return;
    }
    if(renderMode == RENDER_MODE_GRADIENT_FILL) {
        outColor = db_gradient_fill_color(row_i);
        return;
    }
    if(renderMode == RENDER_MODE_SNAKE_GRID) {
        ivec2 history_coord = ivec2(gl_FragCoord.xy);
        vec3 prior_color = texelFetch(uHistoryTex, history_coord, 0).rgb;
        outColor = db_snake_grid_color(row_i, db_col_from_frag_coord(), prior_color);
        return;
    }
    if(renderMode == RENDER_MODE_RECT_SNAKE) {
        ivec2 history_coord = ivec2(gl_FragCoord.xy);
        vec3 prior_color = texelFetch(uHistoryTex, history_coord, 0).rgb;
        outColor = db_rect_snake_color(row_i, db_col_from_frag_coord(), prior_color);
        return;
    }
    outColor = vColor;
}
