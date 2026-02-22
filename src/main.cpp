#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VK_BENCH_SHADER_DIR
#define VK_BENCH_SHADER_DIR "./shaders"
#endif

struct Config {
  bool headless = false;
  uint32_t frames = 300;
  uint32_t warmup = 60;
  uint32_t vsync = 0;
  std::string out = "results.json";
  std::string scene = "triangle";
  uint32_t width = 1920;
  uint32_t height = 1080;
};

struct Stats {
  double avg = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
};

struct BufferWithMemory {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct TriangleResources {
  VkImage color_image = VK_NULL_HANDLE;
  VkDeviceMemory color_memory = VK_NULL_HANDLE;
  VkImageView color_view = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
};

[[noreturn]] void fail(const std::string &msg) {
  throw std::runtime_error(msg);
}

Stats summarize(std::vector<double> values) {
  if (values.empty()) {
    return {};
  }
  std::sort(values.begin(), values.end());
  const double sum = std::accumulate(values.begin(), values.end(), 0.0);
  const auto idx50 = static_cast<size_t>(0.50 * (values.size() - 1));
  const auto idx95 = static_cast<size_t>(0.95 * (values.size() - 1));
  return {sum / static_cast<double>(values.size()), values[idx50],
          values[idx95]};
}

Config parse_args(int argc, char **argv) {
  Config cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto read = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        fail("Missing value for " + name);
      }
      return argv[++i];
    };

    if (arg == "--headless") {
      cfg.headless = true;
    } else if (arg == "--frames") {
      cfg.frames = std::stoul(read("--frames"));
    } else if (arg == "--warmup") {
      cfg.warmup = std::stoul(read("--warmup"));
    } else if (arg == "--vsync") {
      cfg.vsync = std::stoul(read("--vsync"));
    } else if (arg == "--out") {
      cfg.out = read("--out");
    } else if (arg == "--scene") {
      cfg.scene = read("--scene");
    } else if (arg == "--resolution") {
      const auto value = read("--resolution");
      const auto x = value.find('x');
      if (x == std::string::npos) {
        fail("Expected WIDTHxHEIGHT for --resolution");
      }
      cfg.width = std::stoul(value.substr(0, x));
      cfg.height = std::stoul(value.substr(x + 1));
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "vk-bench --headless --frames N --warmup N --vsync 0|1 --out "
             "file.json --scene triangle|million-tris|compute-copy\n";
      std::exit(0);
    } else {
      fail("Unknown argument: " + arg);
    }
  }
  return cfg;
}

std::vector<uint32_t> read_spirv(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    fail("Failed to open shader: " + path);
  }
  const auto size = file.tellg();
  if (size <= 0 || size % 4 != 0) {
    fail("Invalid SPIR-V file size: " + path);
  }
  std::vector<uint32_t> code(static_cast<size_t>(size / 4));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char *>(code.data()), size);
  return code;
}

uint32_t find_memory_type(VkPhysicalDevice physical, uint32_t type_bits,
                          VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties mem_props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
    const bool type_ok = (type_bits & (1u << i)) != 0;
    const bool props_ok =
        (mem_props.memoryTypes[i].propertyFlags & properties) == properties;
    if (type_ok && props_ok) {
      return i;
    }
  }
  fail("No matching memory type found");
}

BufferWithMemory create_buffer(VkPhysicalDevice physical, VkDevice device,
                               VkDeviceSize size, VkBufferUsageFlags usage) {
  BufferWithMemory out{};

  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = usage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device, &bci, nullptr, &out.buffer) != VK_SUCCESS) {
    fail("vkCreateBuffer failed");
  }

  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(device, out.buffer, &req);

  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = find_memory_type(physical, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(device, &alloc, nullptr, &out.memory) != VK_SUCCESS) {
    fail("vkAllocateMemory for buffer failed");
  }

  if (vkBindBufferMemory(device, out.buffer, out.memory, 0) != VK_SUCCESS) {
    fail("vkBindBufferMemory failed");
  }

  return out;
}

void destroy_buffer(VkDevice device, BufferWithMemory &buffer) {
  if (buffer.buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, buffer.buffer, nullptr);
  }
  if (buffer.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, buffer.memory, nullptr);
  }
}

TriangleResources create_triangle_resources(VkPhysicalDevice physical,
                                            VkDevice device, uint32_t width,
                                            uint32_t height) {
  TriangleResources out{};

  VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = VK_FORMAT_R8G8B8A8_UNORM;
  ici.extent = {width, height, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (vkCreateImage(device, &ici, nullptr, &out.color_image) != VK_SUCCESS) {
    fail("vkCreateImage failed");
  }

  VkMemoryRequirements image_req{};
  vkGetImageMemoryRequirements(device, out.color_image, &image_req);

  VkMemoryAllocateInfo image_alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  image_alloc.allocationSize = image_req.size;
  image_alloc.memoryTypeIndex = find_memory_type(
      physical, image_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(device, &image_alloc, nullptr, &out.color_memory) !=
      VK_SUCCESS) {
    fail("vkAllocateMemory for image failed");
  }

  if (vkBindImageMemory(device, out.color_image, out.color_memory, 0) !=
      VK_SUCCESS) {
    fail("vkBindImageMemory failed");
  }

  VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  ivci.image = out.color_image;
  ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ivci.format = VK_FORMAT_R8G8B8A8_UNORM;
  ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ivci.subresourceRange.levelCount = 1;
  ivci.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device, &ivci, nullptr, &out.color_view) !=
      VK_SUCCESS) {
    fail("vkCreateImageView failed");
  }

  VkAttachmentDescription color_attachment{};
  color_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_ref{};
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;

  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &color_attachment;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &subpass;

  if (vkCreateRenderPass(device, &rpci, nullptr, &out.render_pass) !=
      VK_SUCCESS) {
    fail("vkCreateRenderPass failed");
  }

  VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fbci.renderPass = out.render_pass;
  fbci.attachmentCount = 1;
  fbci.pAttachments = &out.color_view;
  fbci.width = width;
  fbci.height = height;
  fbci.layers = 1;
  if (vkCreateFramebuffer(device, &fbci, nullptr, &out.framebuffer) !=
      VK_SUCCESS) {
    fail("vkCreateFramebuffer failed");
  }

  const auto vert =
      read_spirv(std::string(VK_BENCH_SHADER_DIR) + "/triangle.vert.spv");
  const auto frag =
      read_spirv(std::string(VK_BENCH_SHADER_DIR) + "/triangle.frag.spv");

  VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  smci.codeSize = vert.size() * sizeof(uint32_t);
  smci.pCode = vert.data();
  VkShaderModule vert_module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &smci, nullptr, &vert_module) !=
      VK_SUCCESS) {
    fail("vkCreateShaderModule (vert) failed");
  }

  smci.codeSize = frag.size() * sizeof(uint32_t);
  smci.pCode = frag.data();
  VkShaderModule frag_module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &smci, nullptr, &frag_module) !=
      VK_SUCCESS) {
    fail("vkCreateShaderModule (frag) failed");
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert_module;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag_module;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vertex_input{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamic_states;

  VkPipelineLayoutCreateInfo plci{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  if (vkCreatePipelineLayout(device, &plci, nullptr, &out.pipeline_layout) !=
      VK_SUCCESS) {
    fail("vkCreatePipelineLayout failed");
  }

  VkGraphicsPipelineCreateInfo gpci{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vertex_input;
  gpci.pInputAssemblyState = &ia;
  gpci.pViewportState = &viewport_state;
  gpci.pRasterizationState = &raster;
  gpci.pMultisampleState = &ms;
  gpci.pColorBlendState = &blend;
  gpci.pDynamicState = &dynamic;
  gpci.layout = out.pipeline_layout;
  gpci.renderPass = out.render_pass;
  gpci.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                &out.pipeline) != VK_SUCCESS) {
    fail("vkCreateGraphicsPipelines failed");
  }

  vkDestroyShaderModule(device, vert_module, nullptr);
  vkDestroyShaderModule(device, frag_module, nullptr);
  return out;
}

void destroy_triangle_resources(VkDevice device, TriangleResources &resources) {
  if (resources.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, resources.pipeline, nullptr);
  }
  if (resources.pipeline_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, resources.pipeline_layout, nullptr);
  }
  if (resources.framebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device, resources.framebuffer, nullptr);
  }
  if (resources.render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, resources.render_pass, nullptr);
  }
  if (resources.color_view != VK_NULL_HANDLE) {
    vkDestroyImageView(device, resources.color_view, nullptr);
  }
  if (resources.color_image != VK_NULL_HANDLE) {
    vkDestroyImage(device, resources.color_image, nullptr);
  }
  if (resources.color_memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, resources.color_memory, nullptr);
  }
}

int main(int argc, char **argv) {
  try {
    const Config cfg = parse_args(argc, argv);

    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "vk-bench";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
    app_info.pEngineName = "none";
    app_info.engineVersion = VK_MAKE_VERSION(0, 2, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instance_ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_ci.pApplicationInfo = &app_info;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instance_ci, nullptr, &instance) != VK_SUCCESS) {
      fail("vkCreateInstance failed");
    }

    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
    if (gpu_count == 0) {
      fail("No Vulkan devices found");
    }
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data());
    VkPhysicalDevice physical = gpus[0];

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_family_count,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> queue_props(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_family_count,
                                             queue_props.data());

    std::optional<uint32_t> queue_index;
    for (uint32_t i = 0; i < queue_family_count; ++i) {
      if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        queue_index = i;
        break;
      }
    }
    if (!queue_index.has_value()) {
      fail("No graphics queue family found");
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = *queue_index;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS) {
      fail("vkCreateDevice failed");
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, *queue_index, 0, &queue);

    VkCommandPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_ci.queueFamilyIndex = *queue_index;
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(device, &pool_ci, nullptr, &pool);

    VkCommandBufferAllocateInfo alloc{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &alloc, &cmd);

    VkQueryPoolCreateInfo query_ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_ci.queryCount = 2;
    VkQueryPool query_pool = VK_NULL_HANDLE;
    vkCreateQueryPool(device, &query_ci, nullptr, &query_pool);

    const bool graphics_scene =
        cfg.scene == "triangle" || cfg.scene == "million-tris";
    TriangleResources triangle{};
    if (graphics_scene) {
      triangle =
          create_triangle_resources(physical, device, cfg.width, cfg.height);
    }

    BufferWithMemory src{};
    BufferWithMemory dst{};
    if (cfg.scene == "compute-copy") {
      const VkDeviceSize buffer_size = 64ULL * 1024ULL * 1024ULL;
      src = create_buffer(physical, device, buffer_size,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT);
      dst = create_buffer(physical, device, buffer_size,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    std::vector<double> gpu_ms;
    std::vector<double> cpu_ms;

    auto run_frame = [&](bool record) {
      vkResetCommandBuffer(cmd, 0);
      VkCommandBufferBeginInfo begin{
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      vkBeginCommandBuffer(cmd, &begin);

      vkCmdResetQueryPool(cmd, query_pool, 0, 2);
      vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool,
                          0);

      if (cfg.scene == "triangle" || cfg.scene == "million-tris") {
        VkClearValue clear{};
        clear.color.float32[0] = 0.02f;
        clear.color.float32[1] = 0.02f;
        clear.color.float32[2] = 0.02f;
        clear.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass = triangle.render_pass;
        rpbi.framebuffer = triangle.framebuffer;
        rpbi.renderArea.extent = {cfg.width, cfg.height};
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          triangle.pipeline);

        VkViewport viewport{};
        viewport.width = static_cast<float>(cfg.width);
        viewport.height = static_cast<float>(cfg.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = {cfg.width, cfg.height};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const uint32_t triangle_count =
            (cfg.scene == "triangle") ? 1u : 1'000'000u;
        vkCmdDraw(cmd, 3, triangle_count, 0, 0);
        vkCmdEndRenderPass(cmd);
      } else {
        VkBufferCopy copy{};
        copy.size = 4ULL * 1024ULL * 1024ULL;
        for (uint32_t i = 0; i < 16384; ++i) {
          vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &copy);
        }
      }

      vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool,
                          1);
      vkEndCommandBuffer(cmd);

      VkFenceCreateInfo fence_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      VkFence fence = VK_NULL_HANDLE;
      vkCreateFence(device, &fence_ci, nullptr, &fence);

      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &cmd;

      const auto cpu_start = std::chrono::high_resolution_clock::now();
      vkQueueSubmit(queue, 1, &submit, fence);
      const auto cpu_end = std::chrono::high_resolution_clock::now();

      vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

      uint64_t timestamps[2] = {0, 0};
      vkGetQueryPoolResults(device, query_pool, 0, 2, sizeof(timestamps),
                            timestamps, sizeof(uint64_t),
                            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
      vkDestroyFence(device, fence, nullptr);

      if (record) {
        const double cpu_time =
            std::chrono::duration<double, std::milli>(cpu_end - cpu_start)
                .count();
        const double gpu_time =
            static_cast<double>(timestamps[1] - timestamps[0]) *
            props.limits.timestampPeriod * 1e-6;
        cpu_ms.push_back(cpu_time);
        gpu_ms.push_back(gpu_time);
      }
    };

    for (uint32_t i = 0; i < cfg.warmup; ++i) {
      run_frame(false);
    }
    for (uint32_t i = 0; i < cfg.frames; ++i) {
      run_frame(true);
    }

    const Stats cpu = summarize(cpu_ms);
    const Stats gpu = summarize(gpu_ms);

    std::ofstream out(cfg.out);
    out << std::fixed << std::setprecision(4);
    out << "{\n";
    out << "  \"scene\": \"" << cfg.scene << "\",\n";
    out << "  \"headless\": " << (cfg.headless ? "true" : "false") << ",\n";
    out << "  \"frames\": " << cfg.frames << ",\n";
    out << "  \"warmup\": " << cfg.warmup << ",\n";
    out << "  \"vsync\": " << cfg.vsync << ",\n";
    out << "  \"resolution\": \"" << cfg.width << "x" << cfg.height << "\",\n";
    out << "  \"device_name\": \"" << props.deviceName << "\",\n";
    out << "  \"driver_version\": " << props.driverVersion << ",\n";
    out << "  \"cpu_frame_time_ms\": {\"avg\": " << cpu.avg
        << ", \"p50\": " << cpu.p50 << ", \"p95\": " << cpu.p95 << "},\n";
    out << "  \"gpu_frame_time_ms\": {\"avg\": " << gpu.avg
        << ", \"p50\": " << gpu.p50 << ", \"p95\": " << gpu.p95 << "}\n";
    out << "}\n";

    std::cout << "Wrote benchmark results to " << cfg.out << "\n";

    destroy_buffer(device, src);
    destroy_buffer(device, dst);
    destroy_triangle_resources(device, triangle);
    vkDestroyQueryPool(device, query_pool, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
