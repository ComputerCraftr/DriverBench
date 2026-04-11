#include "vk_runtime_internal.h"

#include <stdint.h>

#include "core/db_poll_policy.h"
#include "vk_init_internal.h"
#include "vk_internal.h"
#include <vulkan/vulkan_core.h>

typedef struct {
    VkDevice device;
    VkFence fence;
} db_vk_fence_wait_context_t;

typedef struct {
    VkDevice device;
    VkSwapchainKHR swapchain;
    VkSemaphore semaphore;
    uint32_t *image_index;
} db_vk_acquire_wait_context_t;

static db_sync_wait_result_t db_vk_fence_wait_attempt(void *user_data,
                                                      uint64_t timeout_ns) {
    const db_vk_fence_wait_context_t *const context =
        (const db_vk_fence_wait_context_t *)user_data;
    const VkResult result = vkWaitForFences(
        context->device, 1U, &context->fence, VK_TRUE, timeout_ns);
    if (result == VK_SUCCESS) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_COMPLETED, 0U, 0U,
                                        (uint32_t)result, "fence_signaled");
    }
    if (result == VK_TIMEOUT) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_TIMEOUT, 0U, 0U,
                                        (uint32_t)result, "fence_pending");
    }
    return db_sync_wait_result_make(DB_SYNC_WAIT_FAILED, 0U, 0U,
                                    (uint32_t)result,
                                    db_vk_result_name(result));
}

static db_sync_wait_result_t db_vk_acquire_wait_attempt(void *user_data,
                                                        uint64_t timeout_ns) {
    const db_vk_acquire_wait_context_t *const context =
        (const db_vk_acquire_wait_context_t *)user_data;
    const VkResult result = vkAcquireNextImageKHR(
        context->device, context->swapchain, timeout_ns, context->semaphore,
        VK_NULL_HANDLE, context->image_index);
    if (result == VK_TIMEOUT) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_TIMEOUT, 0U, 0U,
                                        (uint32_t)result, "image_pending");
    }
    if ((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR) ||
        (result == VK_ERROR_OUT_OF_DATE_KHR)) {
        return db_sync_wait_result_make(DB_SYNC_WAIT_COMPLETED, 0U, 0U,
                                        (uint32_t)result,
                                        db_vk_result_name(result));
    }
    return db_sync_wait_result_make(DB_SYNC_WAIT_FAILED, 0U, 0U,
                                    (uint32_t)result,
                                    db_vk_result_name(result));
}

VkResult db_vk_wait_fence(VkDevice device, VkFence fence,
                          db_progress_policy_id_t policy_id,
                          const char *operation) {
    db_vk_fence_wait_context_t context = {.device = device, .fence = fence};
    const db_sync_wait_result_t result =
        db_progress_execute(policy_id, db_vk_fence_wait_attempt, &context);
    db_progress_log_outcome(BACKEND_NAME, operation, policy_id, &result);
    if (result.status == DB_SYNC_WAIT_COMPLETED) {
        return (VkResult)result.native_result;
    }
    if (result.status == DB_SYNC_WAIT_TIMEOUT) {
        return VK_TIMEOUT;
    }
    return (VkResult)result.native_result;
}

VkResult db_vk_acquire_next_image(VkDevice device, VkSwapchainKHR swapchain,
                                  VkSemaphore semaphore,
                                  uint32_t *out_image_index) {
    db_vk_acquire_wait_context_t context = {
        .device = device,
        .swapchain = swapchain,
        .semaphore = semaphore,
        .image_index = out_image_index,
    };
    const db_sync_wait_result_t result = db_progress_execute(
        DB_PROGRESS_VK_ACQUIRE_IMAGE, db_vk_acquire_wait_attempt, &context);
    db_progress_log_outcome(BACKEND_NAME, "acquire_next_image",
                            DB_PROGRESS_VK_ACQUIRE_IMAGE, &result);
    if (result.status == DB_SYNC_WAIT_COMPLETED) {
        return (VkResult)result.native_result;
    }
    if (result.status == DB_SYNC_WAIT_TIMEOUT) {
        return VK_TIMEOUT;
    }
    return (VkResult)result.native_result;
}
