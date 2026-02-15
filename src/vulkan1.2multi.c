#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#ifndef VERT_SPV_PATH
#define VERT_SPV_PATH "rect.vert.spv"
#endif
#ifndef FRAG_SPV_PATH
#define FRAG_SPV_PATH "rect.frag.spv"
#endif

#define BENCH_BANDS 16u
#define BENCH_FRAMES 600u
#define BACKEND_NAME "vulkan"

static void failf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s][error] ", BACKEND_NAME);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(EXIT_FAILURE);
}

static void infof(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "[%s][info] ", BACKEND_NAME);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    va_end(ap);
}

static const char *vk_result_name(VkResult r) {
    switch (r) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
        return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
        return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:
        return "VK_RESULT_UNKNOWN";
    }
}

static void vk_fail(const char *expr, VkResult r, const char *file, int line) {
    failf("%s failed: %s (%d) at %s:%d", expr, vk_result_name(r), (int)r, file,
          line);
}

#define VK_CHECK(x)                                                           \
    do {                                                                      \
        VkResult _r = (x);                                                    \
        if (_r != VK_SUCCESS)                                                 \
            vk_fail(#x, _r, __FILE__, __LINE__);                              \
    } while (0)

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint8_t *read_file(const char *path, size_t *out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f)
        failf("Failed to open shader file: %s", path);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
        failf("Failed to read shader file: %s", path);
    fclose(f);
    *out_sz = (size_t)sz;
    return buf;
}

typedef struct {
    float offsetNDC[2];
    float scaleNDC[2];
    float color[4];
} PushConstants;

typedef struct {
    VkPhysicalDeviceGroupProperties grp;
    uint32_t presentableMask; // which physical devices can present to the
                              // surface (bitmask)
} DeviceGroupInfo;

static int has_ext(const char *name, uint32_t n, VkExtensionProperties *exts) {
    for (uint32_t i = 0; i < n; i++)
        if (strcmp(exts[i].extensionName, name) == 0)
            return 1;
    return 0;
}

int main(void) {
    // ---------------- Window ----------------
    if (!glfwInit())
        failf("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *win = glfwCreateWindow(
        1000, 600, "Vulkan 1.2 opportunistic multi-GPU (device groups)", NULL,
        NULL);
    if (!win)
        failf("glfwCreateWindow failed");

    // ---------------- Instance ----------------
    uint32_t glfwExtCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

    const char *instExts[16];
    uint32_t instExtN = 0;
    for (uint32_t i = 0; i < glfwExtCount; i++)
        instExts[instExtN++] = glfwExts[i];
    instExts[instExtN++] =
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "multi_gpu_2d";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = {.sType =
                                    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = instExtN;
    ici.ppEnabledExtensionNames = instExts;

    VkInstance instance;
    VK_CHECK(vkCreateInstance(&ici, NULL, &instance));

    VkSurfaceKHR surface;
    VK_CHECK(glfwCreateWindowSurface(instance, win, NULL, &surface));

    // ---------------- Enumerate physical devices + groups ----------------
    uint32_t physN = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physN, NULL));
    if (physN == 0)
        failf("No Vulkan physical devices found");
    VkPhysicalDevice *phys =
        (VkPhysicalDevice *)calloc(physN, sizeof(VkPhysicalDevice));
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physN, phys));

    // Enumerate device groups
    uint32_t grpN = 0;
    VK_CHECK(vkEnumeratePhysicalDeviceGroups(instance, &grpN, NULL));
    VkPhysicalDeviceGroupProperties *grps =
        (VkPhysicalDeviceGroupProperties *)calloc(
            grpN, sizeof(VkPhysicalDeviceGroupProperties));
    for (uint32_t i = 0; i < grpN; i++)
        grps[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    VK_CHECK(vkEnumeratePhysicalDeviceGroups(instance, &grpN, grps));

    // Find a group with >=2 devices and at least one presentable device
    DeviceGroupInfo best = {0};
    int haveGroup = 0;

    for (uint32_t gi = 0; gi < grpN; gi++) {
        VkPhysicalDeviceGroupProperties *g = &grps[gi];
        if (g->physicalDeviceCount < 2)
            continue;

        // Determine which devices in group can present to our surface (need
        // queue family + surface support)
        uint32_t mask = 0;
        for (uint32_t di = 0; di < g->physicalDeviceCount; di++) {
            VkPhysicalDevice pd = g->physicalDevices[di];
            uint32_t qfN = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfN, NULL);
            VkQueueFamilyProperties *qf = (VkQueueFamilyProperties *)calloc(
                qfN, sizeof(VkQueueFamilyProperties));
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfN, qf);

            for (uint32_t qi = 0; qi < qfN; qi++) {
                VkBool32 supp = 0;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, qi, surface, &supp);
                if (supp && (qf[qi].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    mask |= (1u << di);
                    break;
                }
            }
            free(qf);
        }

        if (mask == 0)
            continue;

        best.grp = *g;
        best.presentableMask = mask;
        haveGroup = 1;
        break; // keep first usable group for simplicity
    }

    // Choose physical device(s)
    VkPhysicalDeviceGroupProperties chosenGrp = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES};
    uint32_t chosenCount = 1;
    VkPhysicalDevice chosenPhys[8] = {0};
    uint32_t presentMask = 1;

    if (haveGroup) {
        chosenGrp = best.grp;
        chosenCount = chosenGrp.physicalDeviceCount;
        for (uint32_t i = 0; i < chosenCount; i++)
            chosenPhys[i] = chosenGrp.physicalDevices[i];
        presentMask = best.presentableMask;
        infof("Using device group with %u devices (presentMask=0x%x)",
              chosenCount, presentMask);
    } else {
        chosenPhys[0] = phys[0];
        infof("No usable device group found; running single-GPU");
    }

    // Pick a present device index inside group; prefer device 0 if presentable,
    // else first presentable
    uint32_t presentDevIndex = 0;
    if (haveGroup) {
        if (!(presentMask & 1u)) {
            for (uint32_t i = 0; i < chosenCount; i++) {
                if (presentMask & (1u << i)) {
                    presentDevIndex = i;
                    break;
                }
            }
        }
    }

    VkPhysicalDevice presentPhys = chosenPhys[presentDevIndex];

    // ---------------- Queue family selection (graphics+present)
    // ----------------
    uint32_t qfN = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(presentPhys, &qfN, NULL);
    VkQueueFamilyProperties *qf =
        (VkQueueFamilyProperties *)calloc(qfN, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(presentPhys, &qfN, qf);

    uint32_t gfxQF = UINT32_MAX;
    for (uint32_t i = 0; i < qfN; i++) {
        VkBool32 supp = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(presentPhys, i, surface, &supp);
        if (supp && (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            gfxQF = i;
            break;
        }
    }
    if (gfxQF == UINT32_MAX)
        failf("No graphics+present queue family found");
    free(qf);

    // ---------------- Device creation (with device group pNext if available)
    // ----------------
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = gfxQF;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    const char *devExts[8];
    uint32_t devExtN = 0;
    devExts[devExtN++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    // Device-group functionality is requested via VkDeviceGroupDeviceCreateInfo.

    VkPhysicalDeviceFeatures feats = {0};

    VkDeviceGroupDeviceCreateInfo dgci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO};
    dgci.physicalDeviceCount = chosenCount;
    dgci.pPhysicalDevices = chosenPhys;

    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pQueueCreateInfos = &qci;
    dci.queueCreateInfoCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    dci.enabledExtensionCount = devExtN;
    dci.pEnabledFeatures = &feats;

    if (haveGroup) {
        dci.pNext = &dgci;
    }

    VkDevice device;
    VK_CHECK(vkCreateDevice(presentPhys, &dci, NULL, &device));

    VkQueue queue;
    vkGetDeviceQueue(device, gfxQF, 0, &queue);

    // ---------------- Swapchain ----------------
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(presentPhys, surface, &caps));

    uint32_t fmtN = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(presentPhys, surface, &fmtN,
                                                  NULL));
    VkSurfaceFormatKHR *fmts =
        (VkSurfaceFormatKHR *)calloc(fmtN, sizeof(VkSurfaceFormatKHR));
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(presentPhys, surface, &fmtN,
                                                  fmts));
    VkSurfaceFormatKHR fmt = fmts[0];
    free(fmts);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFF) {
        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        extent.width = (uint32_t)w;
        extent.height = (uint32_t)h;
    }

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // safe default
    uint32_t pmN = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(presentPhys, surface,
                                                       &pmN, NULL));
    VkPresentModeKHR *pms =
        (VkPresentModeKHR *)calloc(pmN, sizeof(VkPresentModeKHR));
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(presentPhys, surface,
                                                       &pmN, pms));
    for (uint32_t i = 0; i < pmN; i++) {
        if (pms[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
    free(pms);

    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface;
    sci.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && sci.minImageCount > caps.maxImageCount)
        sci.minImageCount = caps.maxImageCount;
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = presentMode;
    sci.clipped = VK_TRUE;

    VkSwapchainKHR swapchain;
    VK_CHECK(vkCreateSwapchainKHR(device, &sci, NULL, &swapchain));

    uint32_t imgN = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imgN, NULL));
    VkImage *images = (VkImage *)calloc(imgN, sizeof(VkImage));
    VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &imgN, images));

    // ---------------- Image views ----------------
    VkImageView *views = (VkImageView *)calloc(imgN, sizeof(VkImageView));
    for (uint32_t i = 0; i < imgN; i++) {
        VkImageViewCreateInfo ivci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ivci.image = images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = fmt.format;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &ivci, NULL, &views[i]));
    }

    // ---------------- Render pass ----------------
    VkAttachmentDescription colorAtt = {0};
    colorAtt.format = fmt.format;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {
        .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;

    VkSubpassDependency dep = {0};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &colorAtt;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VkRenderPass renderPass;
    VK_CHECK(vkCreateRenderPass(device, &rpci, NULL, &renderPass));

    // ---------------- Framebuffers ----------------
    VkFramebuffer *fbs = (VkFramebuffer *)calloc(imgN, sizeof(VkFramebuffer));
    for (uint32_t i = 0; i < imgN; i++) {
        VkFramebufferCreateInfo fbci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fbci.renderPass = renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &views[i];
        fbci.width = extent.width;
        fbci.height = extent.height;
        fbci.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device, &fbci, NULL, &fbs[i]));
    }

    // ---------------- Pipeline (rectangles) ----------------
    size_t vsz = 0, fsz = 0;
    uint8_t *vbin = read_file(VERT_SPV_PATH, &vsz);
    uint8_t *fbin = read_file(FRAG_SPV_PATH, &fsz);

    VkShaderModuleCreateInfo smci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkShaderModule vs, fs;
    smci.codeSize = vsz;
    smci.pCode = (const uint32_t *)vbin;
    VK_CHECK(vkCreateShaderModule(device, &smci, NULL, &vs));
    smci.codeSize = fsz;
    smci.pCode = (const uint32_t *)fbin;
    VK_CHECK(vkCreateShaderModule(device, &smci, NULL, &fs));
    free(vbin);
    free(fbin);

    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    // Vertex buffer: a unit quad as two triangles (6 verts), inPos in [0..1]
    float quadVerts[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};

    // Create a tiny host-visible vertex buffer (skipping allocator
    // sophistication)
    VkBuffer vbuf;
    VkDeviceMemory vmem;

    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = sizeof(quadVerts);
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(device, &bci, NULL, &vbuf));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, vbuf, &mr);

    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(presentPhys, &mp);

    uint32_t memIndex = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memIndex = i;
            break;
        }
    }
    if (memIndex == UINT32_MAX)
        failf("No host-visible + host-coherent memory type for vertex buffer");

    VkMemoryAllocateInfo mai = {.sType =
                                    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memIndex;
    VK_CHECK(vkAllocateMemory(device, &mai, NULL, &vmem));
    VK_CHECK(vkBindBufferMemory(device, vbuf, vmem, 0));

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(device, vmem, 0, sizeof(quadVerts), 0, &mapped));
    memcpy(mapped, quadVerts, sizeof(quadVerts));
    vkUnmapMemory(device, vmem);

    VkVertexInputBindingDescription bind = {0};
    bind.binding = 0;
    bind.stride = sizeof(float) * 2;
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr = {0};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vis = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &bind;
    vis.vertexAttributeDescriptionCount = 1;
    vis.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = 0xF;
    cba.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                  VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynStates;

    VkPushConstantRange pcr = {0};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &plci, NULL, &layout));

    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vis;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = layout;
    gp.renderPass = renderPass;
    gp.subpass = 0;

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, NULL,
                                       &pipeline));

    vkDestroyShaderModule(device, vs, NULL);
    vkDestroyShaderModule(device, fs, NULL);

    // ---------------- Command pool/buffers ----------------
    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = gfxQF;

    VkCommandPool cmdPool;
    VK_CHECK(vkCreateCommandPool(device, &cpci, NULL, &cmdPool));

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmd));

    // ---------------- Sync ----------------
    VkSemaphoreCreateInfo sci2 = {.sType =
                                      VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore imageAvail, renderDone;
    VK_CHECK(vkCreateSemaphore(device, &sci2, NULL, &imageAvail));
    VK_CHECK(vkCreateSemaphore(device, &sci2, NULL, &renderDone));

    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence inFlight;
    VK_CHECK(vkCreateFence(device, &fci, NULL, &inFlight));

    // ---------------- Opportunistic scheduler state ----------------
    uint32_t band_owner[64];
    uint32_t gpuCount = haveGroup ? chosenCount : 1;

    // Start: round robin across GPUs
    for (uint32_t b = 0; b < BENCH_BANDS; b++)
        band_owner[b] = b % gpuCount;

    // EMA ms per band per GPU
    double ema_ms_per_band[8];
    for (uint32_t g = 0; g < gpuCount; g++)
        ema_ms_per_band[g] = 0.2; // seed guess

    // Frame budget (ns): approximate 60Hz if FIFO, else still useful heuristic
    const uint64_t budget_ns = 16666666ull;
    const uint64_t safety_ns = 2000000ull; // 2ms

    // ---------------- Main loop ----------------
    uint64_t bench_start = now_ns();
    uint32_t bench_frames = 0;
    for (uint32_t frame = 0;
         frame < BENCH_FRAMES && !glfwWindowShouldClose(win); frame++) {
        glfwPollEvents();

        VK_CHECK(vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &inFlight));

        uint32_t imgIndex = 0;
        VkResult ar =
            vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvail,
                                  VK_NULL_HANDLE, &imgIndex);
        if (ar != VK_SUCCESS) {
            infof("AcquireNextImage returned %s (%d), ending benchmark loop",
                  vk_result_name(ar), (int)ar);
            break;
        }

        VK_CHECK(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo cbi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cmd, &cbi));

        VkClearValue clear;
        clear.color.float32[0] = 0.05f;
        clear.color.float32[1] = 0.05f;
        clear.color.float32[2] = 0.07f;
        clear.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo rbi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = renderPass;
        rbi.framebuffer = fbs[imgIndex];
        rbi.renderArea.extent = extent;
        rbi.clearValueCount = 1;
        rbi.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);

        // Time for animation uses the same nominal frame cadence as OpenGL.
        double t = (double)frame / 60.0;

        // Compute current “eligibility” based on predicted finish time
        uint64_t frameStart = now_ns();
        for (uint32_t g = 1; g < gpuCount; g++) {
            // If this GPU is worse than GPU0 by a lot, drift bands away from it
            double ratio = ema_ms_per_band[g] / ema_ms_per_band[0];
            if (ratio > 1.5) {
                // Opportunistic skip: reassign its bands to GPU0
                for (uint32_t b = 0; b < BENCH_BANDS; b++)
                    if (band_owner[b] == g)
                        band_owner[b] = 0;
            }
        }

        // Draw bands
        for (uint32_t b = 0; b < BENCH_BANDS; b++) {
            uint32_t owner = band_owner[b];
            if (owner >= gpuCount)
                owner = 0;

            // Deadline-aware: if predicted completion too late, give it to GPU0
            uint64_t now = now_ns();
            uint64_t predicted_ns = (uint64_t)(ema_ms_per_band[owner] * 1e6);
            if (owner != 0 &&
                (now + predicted_ns) > (frameStart + budget_ns - safety_ns)) {
                owner = 0;
                band_owner[b] = 0;
            }

            if (haveGroup) {
                // Device mask selects which physical device executes subsequent
                // commands
                vkCmdSetDeviceMask(cmd, (1u << owner));
            }

            // Viewport/scissor for this band
            uint32_t x0 = (extent.width * b) / BENCH_BANDS;
            uint32_t x1 = (extent.width * (b + 1)) / BENCH_BANDS;

            VkViewport vpo;
            vpo.x = 0;
            vpo.y = 0;
            vpo.width = (float)extent.width;
            vpo.height = (float)extent.height;
            vpo.minDepth = 0.0f;
            vpo.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &vpo);

            VkRect2D sc;
            sc.offset.x = (int32_t)x0;
            sc.offset.y = 0;
            sc.extent.width = (uint32_t)(x1 - x0);
            sc.extent.height = extent.height;
            vkCmdSetScissor(cmd, 0, 1, &sc);

            // Push constants map quad into this band in NDC
            float ndc_x0 = (2.0f * (float)x0 / (float)extent.width) - 1.0f;
            float ndc_x1 = (2.0f * (float)x1 / (float)extent.width) - 1.0f;

            PushConstants pc;
            pc.offsetNDC[0] = ndc_x0;
            pc.offsetNDC[1] = -1.0f;
            pc.scaleNDC[0] = (ndc_x1 - ndc_x0);
            pc.scaleNDC[1] = 2.0f;

            // Color varies per band + animated pulse
            float pulse = 0.5f + 0.5f * (float)sin(t * 2.0 + b * 0.3);
            pc.color[0] = pulse * (0.2f + 0.8f * (float)b / (float)BENCH_BANDS);
            pc.color[1] = pulse * 0.6f;
            pc.color[2] = 1.0f - pc.color[0];
            pc.color[3] = 1.0f;

            vkCmdPushConstants(cmd, layout,
                               VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }

        // Reset mask back to GPU0 before ending, for sanity
        if (haveGroup)
            vkCmdSetDeviceMask(cmd, 1u);

        vkCmdEndRenderPass(cmd);
        VK_CHECK(vkEndCommandBuffer(cmd));

        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &imageAvail;
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &renderDone;

        // Submit + fence, then present
        VK_CHECK(vkQueueSubmit(queue, 1, &si, inFlight));

        VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &renderDone;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &imgIndex;
        vkQueuePresentKHR(queue, &pi);

        // Update EMA with “frame time per band” approximation:
        // (This is CPU-side and coarse in this minimal sample; for real
        // accuracy use GPU timestamp queries.)
        uint64_t frameEnd = now_ns();
        double frame_ms = (double)(frameEnd - frameStart) / 1e6;
        double ms_per_band = frame_ms / (double)BENCH_BANDS;

        // We don’t know actual per-GPU contributions without query pools;
        // approximate: GPUs with more assigned bands get credit for more work.
        uint32_t bandsPerGpu[8] = {0};
        for (uint32_t b = 0; b < BENCH_BANDS; b++)
            bandsPerGpu[band_owner[b]]++;

        for (uint32_t g = 0; g < gpuCount; g++) {
            if (bandsPerGpu[g] == 0)
                continue;
            // Heuristic: assume ms scales with number of bands per GPU
            double est = ms_per_band;
            ema_ms_per_band[g] = 0.9 * ema_ms_per_band[g] + 0.1 * est;
        }

        bench_frames++;
    }

    uint64_t bench_end = now_ns();
    double bench_ms = (double)(bench_end - bench_start) / 1e6;
    if (bench_frames > 0) {
        double ms_per_frame = bench_ms / (double)bench_frames;
        double fps = 1000.0 / ms_per_frame;
        printf("Vulkan benchmark: frames=%u bands=%u total_ms=%.2f "
               "ms_per_frame=%.3f fps=%.2f\n",
               bench_frames, BENCH_BANDS, bench_ms, ms_per_frame, fps);
    }

    vkDeviceWaitIdle(device);

    // ---------------- Cleanup ----------------
    vkDestroyFence(device, inFlight, NULL);
    vkDestroySemaphore(device, imageAvail, NULL);
    vkDestroySemaphore(device, renderDone, NULL);

    vkDestroyBuffer(device, vbuf, NULL);
    vkFreeMemory(device, vmem, NULL);

    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, layout, NULL);

    for (uint32_t i = 0; i < imgN; i++)
        vkDestroyFramebuffer(device, fbs[i], NULL);
    free(fbs);

    vkDestroyRenderPass(device, renderPass, NULL);

    for (uint32_t i = 0; i < imgN; i++)
        vkDestroyImageView(device, views[i], NULL);
    free(views);

    free(images);
    vkDestroySwapchainKHR(device, swapchain, NULL);

    vkDestroyCommandPool(device, cmdPool, NULL);

    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    glfwDestroyWindow(win);
    glfwTerminate();

    free(phys);
    free(grps);
    return 0;
}
