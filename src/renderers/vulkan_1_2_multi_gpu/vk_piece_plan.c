#include "vk_internal.h"

#include "core/db_conformance.h"
#include <stddef.h>
#include <stdint.h>

#include "../../core/db_core.h"
#include "../../core/db_frame_plan.h"
#include "../../core/db_geometry.h"
#include "../../core/db_log.h"
#include "../../core/db_numeric.h"
#include "../../core/db_raster_geometry.h"
#include "../../core/db_render_ir.h"
#include "core/db_render_types.h"
#define BACKEND_NAME "renderer_vulkan_1_2_multi_gpu"

#if defined(__GNUC__) || defined(__clang__)
#define DB_VK_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#define DB_VK_NONNULL(...)
#endif

enum { DB_VK_DEFAULT_WORKER_SHARE_BPS = 5000U };

void db_vk_frame_rect_iterator_begin(db_vk_frame_rect_iterator_t *iterator,
                                     const db_frame_plan_t *plan) {
    if (iterator == NULL) {
        return;
    }
    *iterator = (db_vk_frame_rect_iterator_t){.plan = plan};
    if ((plan != NULL) && (plan->external_bindings.count == 0U)) {
        db_render_ir_rect_iterator_begin(&iterator->stream, &plan->rebuild_ir);
    } else if (plan != NULL) {
        iterator->stream_index = 1U;
        db_render_ir_rect_iterator_begin(&iterator->stream, &plan->update_ir);
    }
}

int db_vk_frame_rect_iterator_next(db_vk_frame_rect_iterator_t *iterator,
                                   db_render_ir_fill_t *fill) {
    if ((iterator == NULL) || (iterator->plan == NULL) || (fill == NULL)) {
        return 0;
    }
    for (;;) {
        if (db_render_ir_rect_iterator_next(&iterator->stream, fill) != 0) {
            return 1;
        }
        if (iterator->stream_index == 0U) {
            iterator->stream_index = 1U;
            db_render_ir_rect_iterator_begin(&iterator->stream,
                                             &iterator->plan->update_ir);
            continue;
        }
        return 0;
    }
}

int db_vk_frame_rect_iterator_advance_to(db_vk_frame_rect_iterator_t *iterator,
                                         size_t *next_index,
                                         size_t target_index,
                                         db_render_ir_fill_t *fill) {
    if ((iterator == NULL) || (next_index == NULL) || (fill == NULL) ||
        (target_index < *next_index)) {
        return 0;
    }
    while (*next_index <= target_index) {
        if (db_vk_frame_rect_iterator_next(iterator, fill) == 0) {
            return 0;
        }
        (*next_index)++;
    }
    return 1;
}

size_t db_vk_frame_rect_count(const db_frame_plan_t *plan) {
    size_t count = 0U;
    db_vk_frame_rect_iterator_t iterator = {0};
    db_vk_frame_rect_iterator_begin(&iterator, plan);
    db_render_ir_fill_t fill = {0};
    while (db_vk_frame_rect_iterator_next(&iterator, &fill) != 0) {
        if (count == SIZE_MAX) {
            return 0U;
        }
        count++;
    }
    return count;
}

static int vk_write_instance(const db_frame_plan_t *plan,
                             db_render_ir_rect_t rect,
                             db_render_ir_color_t start_color,
                             db_render_ir_color_t end_color, float mode,
                             int32_t axis_start, int32_t axis_end,
                             db_vk_ir_execute_instance_t *instance) {
    if ((plan == NULL) || (instance == NULL)) {
        return 0;
    }
    db_grid_block_t block = {0};
    if (db_render_ir_rect_to_grid_block(rect, plan->grid_cols, plan->grid_rows,
                                        &block) == 0) {
        return 0;
    }
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;
    db_grid_block_bounds_ndc_for_extent(plan->grid_cols, plan->grid_rows,
                                        &block, &x0, &y0, &x1, &y1);
    float start[3] = {0};
    float end[3] = {0};
    db_rgb_f64_quantize_f16_to_f32_rgb3(start_color.rgba, start);
    db_rgb_f64_quantize_f16_to_f32_rgb3(end_color.rgba, end);
    *instance = (db_vk_ir_execute_instance_t){
        /* Vulkan's positive-height viewport maps logical top to NDC -1. */
        .rect = {x0, -y1, x1 - x0, y1 - y0},
        .start_color = {start[0], start[1], start[2], 1.0F},
        .end_color = {end[0], end[1], end[2], 1.0F},
        .gradient = {mode, db_i32_to_f32(axis_start), db_i32_to_f32(axis_end),
                     db_u32_to_f32(plan->grid_rows)},
    };
    return 1;
}

typedef struct {
    size_t instance_count;
    size_t lookup_word_count;
} vk_instance_requirements_t;

static int vk_require_instances(size_t added,
                                vk_instance_requirements_t *requirements) {
    return db_try_add_size(requirements->instance_count, added,
                           &requirements->instance_count);
}

static int vk_require_lookup_words(size_t added,
                                   vk_instance_requirements_t *requirements) {
    return db_try_add_size(requirements->lookup_word_count, added,
                           &requirements->lookup_word_count);
}

static int vk_require_rect(const db_frame_plan_t *plan,
                           db_render_ir_rect_t rect) {
    db_grid_block_t block = {0};
    return db_render_ir_rect_to_grid_block(rect, plan->grid_cols,
                                           plan->grid_rows, &block);
}

static int vk_accumulate_ir_instance_requirements(
    const db_frame_plan_t *plan, const db_render_ir_view_t *view,
    db_pixel_format_t working_format,
    db_gradient_implementation_t implementation,
    vk_instance_requirements_t *requirements) {
    if ((plan == NULL) || (view == NULL) || (requirements == NULL) ||
        (db_render_ir_validate(view) != DB_RENDER_IR_OK)) {
        return 0;
    }
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->opcode == DB_RENDER_IR_OP_CLEAR) {
            db_render_ir_rect_t target = {0};
            if ((db_render_ir_rect_from_extent(plan->grid_cols, plan->grid_rows,
                                               &target) == 0) ||
                (vk_require_instances(1U, requirements) == 0)) {
                return 0;
            }
            continue;
        }
        if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            for (uint32_t index = 0U; index < fills->fill_count; index++) {
                const db_render_ir_fill_t fill =
                    view->fills[fills->first_fill + index];
                if ((vk_require_rect(plan, fill.rect) == 0) ||
                    (vk_require_instances(1U, requirements) == 0)) {
                    return 0;
                }
            }
            continue;
        }
        if (command->opcode != DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
            continue;
        }
        const db_render_ir_linear_gradient_command_t *const gradient =
            DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                    command);
        if ((command->clip_region == DB_RENDER_IR_INVALID_ID) &&
            ((implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC) ||
             (implementation == DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP))) {
            if ((vk_require_rect(plan, gradient->bounds) == 0) ||
                (vk_require_instances(1U, requirements) == 0)) {
                return 0;
            }
            if (implementation == DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP) {
                const size_t words_per_row =
                    db_pixel_format_u32_words_per_pixel(working_format);
                const size_t row_count =
                    db_positive_i32_to_size_or_zero(gradient->bounds.height);
                size_t added_words = 0U;
                if ((words_per_row == 0U) || (row_count == 0U) ||
                    (db_try_mul_size(row_count, words_per_row, &added_words) ==
                     0) ||
                    (vk_require_lookup_words(added_words, requirements) == 0)) {
                    return 0;
                }
            }
            continue;
        }
        db_render_ir_rect_iterator_t rows = {0};
        db_render_ir_rect_iterator_begin_command(&rows, view, command);
        db_render_ir_fill_t fill = {0};
        while (db_render_ir_rect_iterator_next(&rows, &fill) != 0) {
            if ((vk_require_rect(plan, fill.rect) == 0) ||
                (vk_require_instances(1U, requirements) == 0)) {
                return 0;
            }
        }
    }
    return 1;
}

static size_t vk_write_ir_instances(
    const db_frame_plan_t *plan, const db_render_ir_view_t *view,
    db_vk_ir_execute_instance_t *instances, size_t count, size_t capacity,
    uint32_t *lookup_words, size_t lookup_capacity, size_t *lookup_count,
    db_pixel_format_t working_format,
    db_gradient_implementation_t implementation) {
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        if (command->opcode == DB_RENDER_IR_OP_CLEAR) {
            const db_render_ir_clear_command_t *const clear =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_clear_command_t, command);
            const db_render_ir_rect_t rect = {
                .width = db_checked_u32_to_i32(BACKEND_NAME, "clear_width",
                                               plan->grid_cols),
                .height = db_checked_u32_to_i32(BACKEND_NAME, "clear_height",
                                                plan->grid_rows),
            };
            if ((count >= capacity) ||
                (vk_write_instance(plan, rect, clear->color, clear->color, 0.0F,
                                   0, 0, &instances[count]) == 0)) {
                return SIZE_MAX;
            }
            count++;
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
            const db_render_ir_fill_command_t *const fills =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command);
            for (uint32_t index = 0U; index < fills->fill_count; index++) {
                const db_render_ir_fill_t fill =
                    view->fills[fills->first_fill + index];
                if ((count >= capacity) ||
                    (vk_write_instance(plan, fill.rect, fill.color, fill.color,
                                       0.0F, 0, 0, &instances[count]) == 0)) {
                    return SIZE_MAX;
                }
                count++;
            }
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            if ((implementation == DB_GRADIENT_IMPLEMENTATION_SEMANTIC) &&
                (command->clip_region == DB_RENDER_IR_INVALID_ID)) {
                db_render_ir_color_t start = gradient->start_color;
                db_render_ir_color_t end = gradient->end_color;
                if (gradient->reverse_stops != 0U) {
                    const db_render_ir_color_t swap = start;
                    start = end;
                    end = swap;
                }
                if ((count >= capacity) ||
                    (vk_write_instance(plan, gradient->bounds, start, end, 1.0F,
                                       gradient->axis_start, gradient->axis_end,
                                       &instances[count]) == 0)) {
                    return SIZE_MAX;
                }
                count++;
                continue;
            }
            if ((implementation == DB_GRADIENT_IMPLEMENTATION_EXACT_LOOKUP) &&
                (command->clip_region == DB_RENDER_IR_INVALID_ID) &&
                (lookup_words != NULL) && (lookup_count != NULL)) {
                const size_t words_per_row =
                    db_pixel_format_u32_words_per_pixel(working_format);
                if (words_per_row == 0U) {
                    return SIZE_MAX;
                }
                const size_t row_count = db_checked_int_to_size(
                    BACKEND_NAME, "gradient_rows", gradient->bounds.height);
                size_t required_words = 0U;
                if ((db_try_mul_size(row_count, words_per_row,
                                     &required_words) == 0) ||
                    (*lookup_count > lookup_capacity) ||
                    (required_words > lookup_capacity - *lookup_count) ||
                    (count >= capacity)) {
                    return SIZE_MAX;
                }
                const size_t first_word = *lookup_count;
                for (size_t row_offset = 0U; row_offset < row_count;
                     row_offset++) {
                    const int32_t row =
                        gradient->bounds.y +
                        db_checked_size_to_i32(
                            BACKEND_NAME, "gradient_row_offset", row_offset);
                    const db_render_ir_color_t color =
                        db_render_ir_linear_gradient_color_at(gradient, row);
                    if (words_per_row == 1U) {
                        lookup_words[(*lookup_count)++] =
                            db_pack_rgba8888_from_rgb01(
                                color.rgba[0], color.rgba[1], color.rgba[2],
                                UINT8_MAX);
                    } else {
                        const uint16_t red =
                            db_f64_to_f16_via_f32(color.rgba[0]);
                        const uint16_t green =
                            db_f64_to_f16_via_f32(color.rgba[1]);
                        const uint16_t blue =
                            db_f64_to_f16_via_f32(color.rgba[2]);
                        lookup_words[(*lookup_count)++] =
                            (uint32_t)red | ((uint32_t)green << 16U);
                        lookup_words[(*lookup_count)++] =
                            (uint32_t)blue | ((uint32_t)DB_F16_ONE << 16U);
                    }
                }
                const float exact_mode = (words_per_row == 1U) ? 2.0F : 3.0F;
                if (vk_write_instance(
                        plan, gradient->bounds, gradient->start_color,
                        gradient->end_color, exact_mode, gradient->bounds.y,
                        db_checked_size_to_i32(BACKEND_NAME,
                                               "lookup_first_word", first_word),
                        &instances[count]) == 0) {
                    return SIZE_MAX;
                }
                count++;
                continue;
            }
            db_render_ir_rect_iterator_t rows = {0};
            db_render_ir_rect_iterator_begin_command(&rows, view, command);
            db_render_ir_fill_t fill = {0};
            while (db_render_ir_rect_iterator_next(&rows, &fill) != 0) {
                if ((count >= capacity) ||
                    (vk_write_instance(plan, fill.rect, fill.color, fill.color,
                                       0.0F, 0, 0, &instances[count]) == 0)) {
                    return SIZE_MAX;
                }
                count++;
            }
        }
    }
    return count;
}

size_t db_vk_write_frame_instances_for_gradient_path(
    const db_frame_plan_t *plan, db_vk_ir_execute_instance_t *instances,
    size_t instance_capacity, int semantic_gradient) {
    if ((plan == NULL) || (instances == NULL)) {
        return 0U;
    }
    return db_vk_write_frame_instances_for_implementation(
        plan, instances, instance_capacity, NULL, 0U, NULL,
        DB_PIXEL_FORMAT_RGBA8,
        (semantic_gradient != 0) ? DB_GRADIENT_IMPLEMENTATION_SEMANTIC
                                 : DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES);
}

size_t db_vk_write_frame_instances_for_implementation(
    const db_frame_plan_t *plan, db_vk_ir_execute_instance_t *instances,
    size_t instance_capacity, uint32_t *lookup_words, size_t lookup_capacity,
    size_t *lookup_word_count, db_pixel_format_t working_format,
    db_gradient_implementation_t implementation) {
    if ((plan == NULL) || (instances == NULL)) {
        return 0U;
    }
    vk_instance_requirements_t requirements = {0};
    if (((plan->external_bindings.count == 0U) &&
         (vk_accumulate_ir_instance_requirements(plan, &plan->rebuild_ir,
                                                 working_format, implementation,
                                                 &requirements) == 0)) ||
        (vk_accumulate_ir_instance_requirements(plan, &plan->update_ir,
                                                working_format, implementation,
                                                &requirements) == 0) ||
        (requirements.instance_count > instance_capacity) ||
        (requirements.lookup_word_count > lookup_capacity) ||
        ((requirements.lookup_word_count > 0U) &&
         ((lookup_words == NULL) || (lookup_word_count == NULL)))) {
        return 0U;
    }
    size_t local_lookup_count = 0U;
    size_t *const lookup_count = &local_lookup_count;
    size_t count = 0U;
    if (plan->external_bindings.count == 0U) {
        count = vk_write_ir_instances(plan, &plan->rebuild_ir, instances, count,
                                      instance_capacity, lookup_words,
                                      lookup_capacity, lookup_count,
                                      working_format, implementation);
    }
    if (count != SIZE_MAX) {
        count = vk_write_ir_instances(plan, &plan->update_ir, instances, count,
                                      instance_capacity, lookup_words,
                                      lookup_capacity, lookup_count,
                                      working_format, implementation);
    }
    if ((count == SIZE_MAX) || (count != requirements.instance_count) ||
        (local_lookup_count != requirements.lookup_word_count)) {
        DB_RUNTIME_FAIL(BACKEND_NAME,
                        "instance write violated successful preflight");
    }
    if (lookup_word_count != NULL) {
        *lookup_word_count = local_lookup_count;
    }
    return count;
}

size_t db_vk_write_frame_instances(const db_frame_plan_t *plan,
                                   db_vk_ir_execute_instance_t *instances,
                                   size_t instance_capacity) {
    return db_vk_write_frame_instances_for_gradient_path(plan, instances,
                                                         instance_capacity, 0);
}

static uint32_t vk_stable_row_owner(uint32_t row, uint32_t row_count,
                                    uint32_t lane_count, const double *costs) {
    if ((row_count == 0U) || (lane_count <= 1U)) {
        return 0U;
    }
    double total_throughput = 0.0;
    for (uint32_t lane = 0U; lane < lane_count; lane++) {
        const double cost = (costs != NULL) ? costs[lane] : 0.0;
        total_throughput += db_f64_reciprocal_positive_finite_or(cost, 1.0);
    }
    double boundary = 0.0;
    for (uint32_t lane = 0U; lane < lane_count; lane++) {
        const double cost = (costs != NULL) ? costs[lane] : 0.0;
        const double throughput =
            db_f64_reciprocal_positive_finite_or(cost, 1.0);
        boundary += (DB_TO_F64(row_count) * throughput) / total_throughput;
        if (DB_TO_F64(row) < boundary) {
            return lane;
        }
    }
    return lane_count - 1U;
}

static size_t vk_ir_instance_count(const db_render_ir_view_t *view,
                                   int semantic_gradient) {
    size_t count = 0U;
    db_render_ir_iterator_t iterator = {0};
    db_render_ir_iterator_begin(&iterator, view);
    const db_render_ir_command_header_t *command = NULL;
    while ((command = db_render_ir_iterator_next(&iterator)) != NULL) {
        size_t added = 0U;
        if (command->opcode == DB_RENDER_IR_OP_CLEAR) {
            added = 1U;
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_RECTS) {
            added =
                (DB_RENDER_IR_COMMAND_AS(db_render_ir_fill_command_t, command))
                    ->fill_count;
        } else if (command->opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) {
            const db_render_ir_linear_gradient_command_t *const gradient =
                DB_RENDER_IR_COMMAND_AS(db_render_ir_linear_gradient_command_t,
                                        command);
            if ((semantic_gradient != 0) &&
                (command->clip_region == DB_RENDER_IR_INVALID_ID)) {
                added = 1U;
            } else if (command->clip_region == DB_RENDER_IR_INVALID_ID) {
                added =
                    db_positive_i32_to_size_or_zero(gradient->bounds.height);
            } else {
                uint32_t clipped_instances = 0U;
                if (db_render_ir_region_row_span_count(
                        view, command->clip_region, &clipped_instances) == 0) {
                    return 0U;
                }
                added = clipped_instances;
            }
        }
        if (added > (SIZE_MAX - count)) {
            return 0U;
        }
        count += added;
    }
    return count;
}

static size_t vk_frame_instance_count(const db_frame_plan_t *plan,
                                      int semantic_gradient) {
    if (plan == NULL) {
        return 0U;
    }
    const size_t rebuild =
        (plan->external_bindings.count == 0U)
            ? vk_ir_instance_count(&plan->rebuild_ir, semantic_gradient)
            : 0U;
    const size_t update =
        vk_ir_instance_count(&plan->update_ir, semantic_gradient);
    return db_size_add_or_zero(rebuild, update);
}

static void vk_build_primary_fallback(const db_frame_plan_t *frame_plan,
                                      uint32_t scheduling_epoch,
                                      uint32_t content_generation,
                                      db_vk_present_piece_t *pieces,
                                      db_vk_lane_assignment_t *assignments,
                                      db_vk_execution_plan_t *out_plan,
                                      size_t geometry_count) {
    pieces[0] = (db_vk_present_piece_t){
        .source_rect = {.extent = {.width = frame_plan->pixel_width,
                                   .height = frame_plan->pixel_height}},
        .destination_rect = {.extent = {.width = frame_plan->pixel_width,
                                        .height = frame_plan->pixel_height}},
        .logical_rect =
            {
                .width = (int32_t)frame_plan->grid_cols,
                .height = (int32_t)frame_plan->grid_rows,
            },
        .instance_count = db_size_to_u32_or_zero(geometry_count),
        .compose_mode = DB_VK_COMPOSE_SAMPLE_NEAREST,
    };
    assignments[0] = (db_vk_lane_assignment_t){
        .piece_count = 1U,
        .slot = frame_plan->frame_index % DB_VK_LANE_SLOT_COUNT,
    };
    *out_plan = (db_vk_execution_plan_t){
        .scheduling_epoch = scheduling_epoch,
        .content_generation = content_generation,
        .pieces = pieces,
        .piece_count = 1U,
        .assignments = assignments,
        .assignment_count = 1U,
        .policy = DB_VK_SCHEDULING_PRIMARY_ONLY,
        .primary_only_fallback = 1,
    };
}

static int vk_execution_plan_arguments_valid(
    const db_frame_plan_t *frame_plan,
    const db_vk_planner_workspace_t *workspace,
    const db_vk_present_piece_t *pieces, size_t piece_capacity,
    const db_vk_lane_assignment_t *assignments, size_t assignment_capacity,
    const db_vk_execution_plan_t *out_plan) {
    return DB_BOOL((frame_plan != NULL) && (workspace != NULL) &&
                   (workspace->ranges != NULL) && (workspace->capacity > 0U) &&
                   (pieces != NULL) && (piece_capacity > 0U) &&
                   (assignments != NULL) && (assignment_capacity > 0U) &&
                   (out_plan != NULL));
}

static int DB_VK_NONNULL(1, 2, 9, 11, 13) vk_build_execution_plan_impl(
    const db_frame_plan_t *frame_plan, db_vk_planner_workspace_t *workspace,
    uint32_t lane_count, db_vk_scheduling_policy_t policy,
    uint32_t worker_share_bps, const double *ema_ms_per_work_unit,
    uint32_t scheduling_epoch, uint32_t content_generation,
    db_vk_present_piece_t *pieces, size_t piece_capacity,
    db_vk_lane_assignment_t *assignments, size_t assignment_capacity,
    db_vk_execution_plan_t *out_plan, int semantic_gradient) {
    lane_count = DB_CLAMP(lane_count, 1U, DB_VK_MAX_LANES);
    const size_t geometry_count =
        vk_frame_instance_count(frame_plan, semantic_gradient);
    if ((frame_plan->update_metadata.status != DB_RENDER_IR_OK) ||
        (frame_plan->rebuild_metadata.status != DB_RENDER_IR_OK) ||
        (geometry_count > DB_VK_MAX_PIECES_PER_FRAME) ||
        (geometry_count > UINT32_MAX)) {
        return 0;
    }

    db_render_ir_command_range_t *const ranges = workspace->ranges;
    const size_t workspace_capacity = workspace->capacity;
    size_t range_count = 0U;
    int range_overflow = 0;
    if (frame_plan->external_bindings.count == 0U) {
        range_count = db_render_ir_collect_command_ranges(
            &frame_plan->rebuild_ir, DB_RENDER_IR_STREAM_REBUILD, ranges,
            workspace_capacity, &range_overflow);
    }
    if (range_overflow == 0) {
        range_count += db_render_ir_collect_command_ranges(
            &frame_plan->update_ir, DB_RENDER_IR_STREAM_UPDATE,
            &ranges[range_count], workspace_capacity - range_count,
            &range_overflow);
    }
    if ((range_overflow != 0) || (range_count > piece_capacity) ||
        (range_count > assignment_capacity)) {
        vk_build_primary_fallback(frame_plan, scheduling_epoch,
                                  content_generation, pieces, assignments,
                                  out_plan, geometry_count);
        return 1;
    }
    workspace->range_high_water =
        DB_MAX(workspace->range_high_water, range_count);

    uint32_t lane_work[DB_VK_MAX_LANES] = {0};
    size_t emitted = 0U;
    size_t instance_first = 0U;
    const int requires_primary =
        DB_BOOL(frame_plan->update_metadata.prior_content_required ||
                frame_plan->rebuild_metadata.prior_content_required);
    for (size_t range_index = 0U; range_index < range_count; range_index++) {
        const db_render_ir_command_range_t range = ranges[range_index];
        const int semantic_range =
            DB_BOOL((semantic_gradient != 0) &&
                    (range.opcode == DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) &&
                    (range.semantic_gradient_eligible != 0U));
        const size_t range_instances = (semantic_range != 0)
                                           ? range.instance_count
                                           : range.fallback_instance_count;
        if ((range.has_bounds == 0U) && (range_instances > 0U)) {
            vk_build_primary_fallback(frame_plan, scheduling_epoch,
                                      content_generation, pieces, assignments,
                                      out_plan, geometry_count);
            return 1;
        }
        if (range_instances == 0U) {
            continue;
        }
        const db_render_ir_rect_t bounds = range.bounds;
        db_grid_block_t grid_block = {0};
        if (db_render_ir_rect_to_grid_block(bounds, frame_plan->grid_cols,
                                            frame_plan->grid_rows,
                                            &grid_block) == 0) {
            vk_build_primary_fallback(frame_plan, scheduling_epoch,
                                      content_generation, pieces, assignments,
                                      out_plan, geometry_count);
            return 1;
        }
        db_damage_block_t pixel_block = {0};
        if (db_grid_block_to_pixel_block(
                frame_plan->grid_cols, frame_plan->grid_rows, &grid_block,
                frame_plan->pixel_width, frame_plan->pixel_height,
                &pixel_block) == 0) {
            continue;
        }
        uint32_t lane = 0U;
        if ((policy == DB_VK_SCHEDULING_STABLE_ROWS) && (lane_count > 1U)) {
            const uint32_t midpoint =
                grid_block.row_start + (grid_block.row_count / 2U);
            if ((lane_count == 2U) && (worker_share_bps > 0U)) {
                const uint64_t primary_rows =
                    ((uint64_t)frame_plan->grid_rows *
                     (uint64_t)(10000U - DB_MIN(worker_share_bps, 10000U))) /
                    10000U;
                lane = DB_BOOL((uint64_t)midpoint >= primary_rows);
            } else {
                lane = vk_stable_row_owner(midpoint, frame_plan->grid_rows,
                                           lane_count, ema_ms_per_work_unit);
            }
        } else if (((policy == DB_VK_SCHEDULING_GREEDY_DAMAGE_CHUNKS) ||
                    (policy == DB_VK_SCHEDULING_THROUGHPUT_WEIGHTED_CHUNKS)) &&
                   (lane_count > 1U)) {
            lane = db_vk_select_owner_for_work(
                lane_count,
                db_grid_block_span_units_or_fail("piece_units", &grid_block),
                UINT64_MAX, 0U, ema_ms_per_work_unit, lane_work);
        }
        const uint32_t units =
            db_grid_block_span_units_or_fail("piece_units", &grid_block);
        lane_work[lane] = db_checked_add_u32(BACKEND_NAME, "lane_work",
                                             lane_work[lane], units);
        const int32_t pixel_x = db_checked_u32_to_i32(BACKEND_NAME, "piece_x",
                                                      pixel_block.col_start);
        const int32_t pixel_y = db_checked_u32_to_i32(BACKEND_NAME, "piece_y",
                                                      pixel_block.row_start);
        pieces[emitted] = (db_vk_present_piece_t){
            .source_rect = {.offset = {.x = pixel_x, .y = pixel_y},
                            .extent = {.width = pixel_block.col_count,
                                       .height = pixel_block.row_count}},
            .destination_rect = {.offset = {.x = pixel_x, .y = pixel_y},
                                 .extent = {.width = pixel_block.col_count,
                                            .height = pixel_block.row_count}},
            .logical_rect = bounds,
            .command_range = range,
            .instance_first = db_checked_size_to_u32(
                BACKEND_NAME, "piece_instance_first", instance_first),
            .instance_count = db_checked_size_to_u32(
                BACKEND_NAME, "piece_instance_count", range_instances),
            .piece_index =
                db_checked_size_to_u32(BACKEND_NAME, "piece_index", emitted),
            .compose_mode = DB_VK_COMPOSE_SAMPLE_NEAREST,
        };
        assignments[emitted] = (db_vk_lane_assignment_t){
            .piece_first = (uint32_t)emitted,
            .piece_count = 1U,
            .lane = lane,
            .slot = frame_plan->frame_index % DB_VK_LANE_SLOT_COUNT,
            .batch = 0U,
        };
        instance_first += range_instances;
        emitted++;
    }
    if (requires_primary != 0) {
        for (size_t index = 0U; index < emitted; index++) {
            assignments[index].lane = 0U;
        }
        policy = DB_VK_SCHEDULING_PRIMARY_ONLY;
    }
    *out_plan = (db_vk_execution_plan_t){
        .scheduling_epoch = scheduling_epoch,
        .content_generation = content_generation,
        .pieces = pieces,
        .piece_count = emitted,
        .assignments = assignments,
        .assignment_count = emitted,
        .policy = policy,
    };
    return 1;
}

int db_vk_build_execution_plan(
    const db_frame_plan_t *frame_plan, db_vk_planner_workspace_t *workspace,
    uint32_t lane_count, db_vk_scheduling_policy_t policy,
    const double *ema_ms_per_work_unit, uint32_t scheduling_epoch,
    uint32_t content_generation, db_vk_present_piece_t *pieces,
    size_t piece_capacity, db_vk_lane_assignment_t *assignments,
    size_t assignment_capacity, db_vk_execution_plan_t *out_plan) {
    if (vk_execution_plan_arguments_valid(frame_plan, workspace, pieces,
                                          piece_capacity, assignments,
                                          assignment_capacity, out_plan) == 0) {
        return 0;
    }
    return vk_build_execution_plan_impl(
        frame_plan, workspace, lane_count, policy,
        DB_VK_DEFAULT_WORKER_SHARE_BPS, ema_ms_per_work_unit, scheduling_epoch,
        content_generation, pieces, piece_capacity, assignments,
        assignment_capacity, out_plan, 0);
}

int db_vk_build_execution_plan_for_gradient_path(
    const db_frame_plan_t *frame_plan, db_vk_planner_workspace_t *workspace,
    uint32_t lane_count, db_vk_scheduling_policy_t policy,
    const double *ema_ms_per_work_unit, uint32_t scheduling_epoch,
    uint32_t content_generation, db_vk_present_piece_t *pieces,
    size_t piece_capacity, db_vk_lane_assignment_t *assignments,
    size_t assignment_capacity, db_vk_execution_plan_t *out_plan,
    int semantic_gradient) {
    if (vk_execution_plan_arguments_valid(frame_plan, workspace, pieces,
                                          piece_capacity, assignments,
                                          assignment_capacity, out_plan) == 0) {
        return 0;
    }
    return vk_build_execution_plan_impl(
        frame_plan, workspace, lane_count, policy,
        DB_VK_DEFAULT_WORKER_SHARE_BPS, ema_ms_per_work_unit, scheduling_epoch,
        content_generation, pieces, piece_capacity, assignments,
        assignment_capacity, out_plan, semantic_gradient);
}

uint32_t db_vk_route_accelerated_gradients_to_primary(
    db_vk_execution_plan_t *execution_plan,
    db_gradient_implementation_t implementation) {
    if ((execution_plan == NULL) ||
        (implementation == DB_GRADIENT_IMPLEMENTATION_ROW_INSTANCES)) {
        return 0U;
    }
    uint32_t routed = 0U;
    for (size_t index = 0U; index < execution_plan->assignment_count; index++) {
        db_vk_lane_assignment_t *const assignment =
            &execution_plan->assignments[index];
        if ((assignment->lane == 0U) ||
            (assignment->piece_first >= execution_plan->piece_count)) {
            continue;
        }
        const db_vk_present_piece_t *const piece =
            &execution_plan->pieces[assignment->piece_first];
        if ((piece->command_range.opcode !=
             DB_RENDER_IR_OP_FILL_LINEAR_GRADIENT) ||
            (piece->command_range.semantic_gradient_eligible == 0U)) {
            continue;
        }
        assignment->lane = 0U;
        routed++;
    }
    return routed;
}

int db_vk_build_execution_plan_with_worker_share(
    const db_frame_plan_t *frame_plan, db_vk_planner_workspace_t *workspace,
    uint32_t lane_count, db_vk_scheduling_policy_t policy,
    uint32_t worker_share_bps, const double *ema_ms_per_work_unit,
    uint32_t scheduling_epoch, uint32_t content_generation,
    db_vk_present_piece_t *pieces, size_t piece_capacity,
    db_vk_lane_assignment_t *assignments, size_t assignment_capacity,
    db_vk_execution_plan_t *out_plan) {
    if (vk_execution_plan_arguments_valid(frame_plan, workspace, pieces,
                                          piece_capacity, assignments,
                                          assignment_capacity, out_plan) == 0) {
        return 0;
    }
    return vk_build_execution_plan_impl(
        frame_plan, workspace, lane_count, policy, worker_share_bps,
        ema_ms_per_work_unit, scheduling_epoch, content_generation, pieces,
        piece_capacity, assignments, assignment_capacity, out_plan, 0);
}

int db_vk_sync_state_transition_valid(db_vk_sync_state_t from,
                                      db_vk_sync_state_t to) {
    switch (from) {
    case DB_VK_SYNC_IDLE:
    case DB_VK_SYNC_PAYLOAD_CONSUMED:
        return DB_BOOL(to == DB_VK_SYNC_SIGNAL_SUBMITTED);
    case DB_VK_SYNC_SIGNAL_SUBMITTED:
        return DB_BOOL(to == DB_VK_SYNC_FD_EXPORTED);
    case DB_VK_SYNC_FD_EXPORTED:
        return DB_BOOL(to == DB_VK_SYNC_FD_IMPORTED);
    case DB_VK_SYNC_FD_IMPORTED:
        return DB_BOOL(to == DB_VK_SYNC_WAIT_SUBMITTED);
    case DB_VK_SYNC_WAIT_SUBMITTED:
        return DB_BOOL(to == DB_VK_SYNC_PAYLOAD_CONSUMED);
    }
    return 0;
}

int db_vk_slot_result_is_current(const db_vk_lane_slot_t *slot,
                                 const db_vk_execution_plan_t *plan,
                                 uint64_t frame_index) {
    return DB_BOOL((slot != NULL) && (plan != NULL) &&
                   (slot->quarantined == 0) &&
                   (slot->content_generation == plan->content_generation) &&
                   (slot->scheduling_epoch == plan->scheduling_epoch) &&
                   (slot->last_applied_frame == frame_index) &&
                   (slot->valid_piece_count > 0U));
}
