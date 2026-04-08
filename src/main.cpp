#include <vulkan/vulkan.h>

#if VK_BENCH_HAS_WINDOW
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct Config {
  bool headless = false;
  uint32_t frames = 300;
  uint32_t warmup = 60;
  uint32_t vsync = 0;
  std::string out = "results.json";
  std::string scene = "triangle";
  uint32_t triangles = 1;
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

struct CaptureTarget {
  VkImage image = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0;
  uint32_t height = 0;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

constexpr uint32_t kMillionTrisTriangleCount = 1'000'000u;
constexpr VkDeviceSize kComputeBufferSize = 64ULL * 1024ULL * 1024ULL;
constexpr uint32_t kComputeLocalSize = 256u;
constexpr uint32_t kComputeInnerIterations = 512u;

struct TriangleResources {
  VkImage color_image = VK_NULL_HANDLE;
  VkDeviceMemory color_memory = VK_NULL_HANDLE;
  VkImageView color_view = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
};

struct ComputeResources {
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
};

#if VK_BENCH_HAS_WINDOW
struct WindowResources {
  GLFWwindow *window = nullptr;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat color_format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
  std::vector<VkImage> images;
  std::vector<VkImageView> views;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
};
#endif

[[noreturn]] void fail(const std::string &msg) {
  throw std::runtime_error(msg);
}

std::string escape_json_string(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const unsigned char c : value) {
    switch (c) {
    case '\"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      if (c < 0x20) {
        constexpr char hex[] = "0123456789abcdef";
        escaped += "\\u00";
        escaped += hex[(c >> 4) & 0x0f];
        escaped += hex[c & 0x0f];
      } else {
        escaped.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  return escaped;
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

bool scene_uses_graphics(const std::string &scene);
bool scene_uses_compute(const std::string &scene);
std::string shader_prefix_for_scene(const std::string &scene);

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

    auto parse_uint32 = [](const std::string &name,
                           const std::string &value) -> uint32_t {
      try {
        const unsigned long v = std::stoul(value);
        if (v > std::numeric_limits<uint32_t>::max()) {
          fail("Value out of range for " + name + ": " + value);
        }
        return static_cast<uint32_t>(v);
      } catch (const std::logic_error &) {
        fail("Invalid integer value for " + name + ": " + value);
      }
    };

    if (arg == "--headless") {
      cfg.headless = true;
    } else if (arg == "--frames") {
      cfg.frames = parse_uint32("--frames", read("--frames"));
    } else if (arg == "--warmup") {
      cfg.warmup = parse_uint32("--warmup", read("--warmup"));
    } else if (arg == "--vsync") {
      cfg.vsync = parse_uint32("--vsync", read("--vsync"));
    } else if (arg == "--out") {
      cfg.out = read("--out");
    } else if (arg == "--scene") {
      cfg.scene = read("--scene");
    } else if (arg == "--triangles") {
      cfg.triangles = parse_uint32("--triangles", read("--triangles"));
    } else if (arg == "--resolution") {
      const auto value = read("--resolution");
      const auto x = value.find('x');
      if (x == std::string::npos) {
        fail("Expected WIDTHxHEIGHT for --resolution");
      }
      cfg.width = parse_uint32("--resolution width", value.substr(0, x));
      cfg.height = parse_uint32("--resolution height", value.substr(x + 1));
      if (cfg.width == 0 || cfg.height == 0) {
        fail("--resolution width and height must be greater than 0");
      }
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "vk-bench --headless --frames N --warmup N --vsync 0|1 --out "
             "file.json --scene triangle|million-tris|compute-copy "
             "[--triangles N] [--resolution WxH]\n";
      std::exit(0);
    } else {
      fail("Unknown argument: " + arg);
    }
  }
  const bool valid_scene = cfg.scene == "triangle" ||
                           cfg.scene == "million-tris" ||
                           cfg.scene == "compute-copy";
  if (!valid_scene) {
    fail("Unsupported scene: " + cfg.scene +
         ". Expected one of: triangle, million-tris, compute-copy");
  }
  if (cfg.scene == "triangle" && cfg.triangles == 0) {
    fail("--triangles must be >= 1 for triangle scene");
  }
  return cfg;
}

std::string pick_shader_dir(const char *argv0, const std::string &scene) {
  namespace fs = std::filesystem;
  const std::string shader_prefix = shader_prefix_for_scene(scene);

  // Ordered lookup keeps runtime flexible across local builds and installed bins.
  std::vector<fs::path> candidates;
  if (const char *env = std::getenv("VK_BENCH_SHADER_DIR"); env && *env) {
    candidates.emplace_back(env);
  }
  if (argv0 != nullptr && std::strlen(argv0) > 0) {
    fs::path exe_path(argv0);
    if (!exe_path.is_absolute()) {
      exe_path = fs::absolute(exe_path);
    }
    candidates.push_back(exe_path.parent_path() / "shaders");
    candidates.push_back(exe_path.parent_path().parent_path() / "shaders");
  }

#ifdef VK_BENCH_SHADER_DIR
  candidates.emplace_back(VK_BENCH_SHADER_DIR);
#endif
  candidates.emplace_back(fs::current_path() / "shaders");

  for (const auto &candidate : candidates) {
    std::error_code ec;
    const bool graphics_ok =
        scene_uses_graphics(scene) &&
        fs::exists(candidate / (shader_prefix + ".vert.spv"), ec) &&
        fs::exists(candidate / (shader_prefix + ".frag.spv"), ec);
    const bool compute_ok =
        scene_uses_compute(scene) &&
        fs::exists(candidate / (shader_prefix + ".comp.spv"), ec);
    if (graphics_ok || compute_ok) {
      return candidate.string();
    }
  }

  std::string searched;
  for (const auto &candidate : candidates) {
    if (!searched.empty()) {
      searched += ", ";
    }
    searched += candidate.string();
  }
  fail("Failed to locate shader directory. Checked: " + searched);
}

bool scene_uses_graphics(const std::string &scene) {
  return scene == "triangle" || scene == "million-tris";
}

bool scene_uses_compute(const std::string &scene) {
  return scene == "compute-copy";
}

std::string shader_prefix_for_scene(const std::string &scene) {
  if (scene == "triangle") {
    return "triangle";
  }
  if (scene == "million-tris") {
    return "million-tris";
  }
  if (scene == "compute-copy") {
    return "compute-copy";
  }
  fail("Unsupported scene: " + scene);
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
                               VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags memory_properties) {
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
  alloc.memoryTypeIndex =
      find_memory_type(physical, req.memoryTypeBits, memory_properties);
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

std::filesystem::path screenshot_path_for_output(const std::string &out_path) {
  namespace fs = std::filesystem;
  fs::path path(out_path);
  path.replace_extension(".bmp");
  return path;
}

void write_bmp(const std::filesystem::path &path, const uint8_t *pixels,
               size_t pixel_bytes, uint32_t width, uint32_t height,
               VkFormat format) {
  const bool rgba =
      format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
  const bool bgra =
      format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
  if (!rgba && !bgra) {
    fail("Screenshot capture only supports R8G8B8A8/B8G8R8A8 render targets");
  }

  const uint32_t row_size = width * 3;
  const uint32_t row_padding = (4 - (row_size % 4)) % 4;
  const uint32_t file_size = 14 + 40 + (row_size + row_padding) * height;

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    fail("Failed to open screenshot output: " + path.string());
  }

  const uint8_t file_header[14] = {
      'B', 'M',
      static_cast<uint8_t>(file_size & 0xff),
      static_cast<uint8_t>((file_size >> 8) & 0xff),
      static_cast<uint8_t>((file_size >> 16) & 0xff),
      static_cast<uint8_t>((file_size >> 24) & 0xff),
      0, 0, 0, 0, 54, 0, 0, 0};
  out.write(reinterpret_cast<const char *>(file_header), sizeof(file_header));

  const uint8_t info_header[40] = {
      40, 0, 0, 0,
      static_cast<uint8_t>(width & 0xff),
      static_cast<uint8_t>((width >> 8) & 0xff),
      static_cast<uint8_t>((width >> 16) & 0xff),
      static_cast<uint8_t>((width >> 24) & 0xff),
      static_cast<uint8_t>(height & 0xff),
      static_cast<uint8_t>((height >> 8) & 0xff),
      static_cast<uint8_t>((height >> 16) & 0xff),
      static_cast<uint8_t>((height >> 24) & 0xff),
      1, 0, 24, 0,
      0, 0, 0, 0,
      0, 0, 0, 0,
      0x13, 0x0b, 0, 0,
      0x13, 0x0b, 0, 0,
      0, 0, 0, 0,
      0, 0, 0, 0};
  out.write(reinterpret_cast<const char *>(info_header), sizeof(info_header));

  const std::vector<uint8_t> padding(row_padding, 0);
  for (int32_t y = static_cast<int32_t>(height) - 1; y >= 0; --y) {
    const uint8_t *row = pixels + static_cast<size_t>(y) * width * pixel_bytes;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t *src = row + static_cast<size_t>(x) * pixel_bytes;
      const uint8_t b = bgra ? src[0] : src[2];
      const uint8_t g = src[1];
      const uint8_t r = bgra ? src[2] : src[0];
      const uint8_t bgr[3] = {b, g, r};
      out.write(reinterpret_cast<const char *>(bgr), sizeof(bgr));
    }
    if (!padding.empty()) {
      out.write(reinterpret_cast<const char *>(padding.data()), padding.size());
    }
  }
}

void transition_image_layout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout old_layout,
                             VkImageLayout new_layout) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

  VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  }

  vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
}

void capture_image_to_bmp(VkPhysicalDevice physical, VkDevice device,
                          VkQueue queue, VkCommandPool pool,
                          const CaptureTarget &target,
                          const std::filesystem::path &path) {
  if (target.image == VK_NULL_HANDLE || target.width == 0 || target.height == 0) {
    fail("Invalid screenshot target");
  }

  const VkDeviceSize buffer_size =
      static_cast<VkDeviceSize>(target.width) * target.height * 4;
  BufferWithMemory staging = create_buffer(
      physical, device, buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  VkCommandBufferAllocateInfo alloc{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc.commandPool = pool;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (vkAllocateCommandBuffers(device, &alloc, &cmd) != VK_SUCCESS) {
    destroy_buffer(device, staging);
    fail("vkAllocateCommandBuffers for screenshot failed");
  }

  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    destroy_buffer(device, staging);
    fail("vkBeginCommandBuffer for screenshot failed");
  }
  transition_image_layout(cmd, target.image, target.layout,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  VkBufferImageCopy copy{};
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.layerCount = 1;
  copy.imageExtent = {target.width, target.height, 1};
  vkCmdCopyImageToBuffer(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         staging.buffer, 1, &copy);
  transition_image_layout(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          target.layout);
  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    destroy_buffer(device, staging);
    fail("vkEndCommandBuffer for screenshot failed");
  }

  VkFenceCreateInfo fence_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence = VK_NULL_HANDLE;
  if (vkCreateFence(device, &fence_ci, nullptr, &fence) != VK_SUCCESS) {
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    destroy_buffer(device, staging);
    fail("vkCreateFence for screenshot failed");
  }

  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS) {
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    destroy_buffer(device, staging);
    fail("vkQueueSubmit for screenshot failed");
  }
  if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    destroy_buffer(device, staging);
    fail("vkWaitForFences for screenshot failed");
  }

  void *mapped = nullptr;
  if (vkMapMemory(device, staging.memory, 0, buffer_size, 0, &mapped) != VK_SUCCESS) {
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    destroy_buffer(device, staging);
    fail("vkMapMemory for screenshot failed");
  }
  write_bmp(path, static_cast<const uint8_t *>(mapped), 4, target.width,
            target.height, target.format);
  vkUnmapMemory(device, staging.memory);

  vkDestroyFence(device, fence, nullptr);
  vkFreeCommandBuffers(device, pool, 1, &cmd);
  destroy_buffer(device, staging);
}

VkShaderModule create_shader_module(VkDevice device,
                                    const std::vector<uint32_t> &spirv) {
  VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  smci.codeSize = spirv.size() * sizeof(uint32_t);
  smci.pCode = spirv.data();

  VkShaderModule shader = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &smci, nullptr, &shader) != VK_SUCCESS) {
    fail("vkCreateShaderModule failed");
  }
  return shader;
}

void create_graphics_pipeline(VkDevice device, VkRenderPass render_pass,
                              const std::string &shader_dir,
                              const std::string &scene,
                              VkPipelineLayout &pipeline_layout,
                              VkPipeline &pipeline) {
  const std::string shader_prefix = shader_prefix_for_scene(scene);
  const auto vert = read_spirv(shader_dir + "/" + shader_prefix + ".vert.spv");
  const auto frag = read_spirv(shader_dir + "/" + shader_prefix + ".frag.spv");

  const VkShaderModule vert_module = create_shader_module(device, vert);
  const VkShaderModule frag_module = create_shader_module(device, frag);

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
  // Viewport/scissor are frame-dependent (swapchain extent vs headless extent).
  VkPipelineDynamicStateCreateInfo dynamic{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamic_states;

  VkPipelineLayoutCreateInfo plci{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  if (vkCreatePipelineLayout(device, &plci, nullptr, &pipeline_layout) !=
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
  gpci.layout = pipeline_layout;
  gpci.renderPass = render_pass;
  gpci.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                &pipeline) != VK_SUCCESS) {
    fail("vkCreateGraphicsPipelines failed");
  }

  vkDestroyShaderModule(device, vert_module, nullptr);
  vkDestroyShaderModule(device, frag_module, nullptr);
}

ComputeResources create_compute_resources(VkDevice device,
                                          const std::string &shader_dir,
                                          const std::string &scene,
                                          VkBuffer storage_buffer,
                                          VkDeviceSize storage_size) {
  ComputeResources out{};

  VkDescriptorSetLayoutBinding storage_binding{};
  storage_binding.binding = 0;
  storage_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  storage_binding.descriptorCount = 1;
  storage_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo layout_ci{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layout_ci.bindingCount = 1;
  layout_ci.pBindings = &storage_binding;
  if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr,
                                  &out.descriptor_set_layout) != VK_SUCCESS) {
    fail("vkCreateDescriptorSetLayout failed");
  }

  VkPushConstantRange push_constant{};
  push_constant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push_constant.offset = 0;
  push_constant.size = sizeof(uint32_t);

  VkPipelineLayoutCreateInfo pipeline_layout_ci{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipeline_layout_ci.setLayoutCount = 1;
  pipeline_layout_ci.pSetLayouts = &out.descriptor_set_layout;
  pipeline_layout_ci.pushConstantRangeCount = 1;
  pipeline_layout_ci.pPushConstantRanges = &push_constant;
  if (vkCreatePipelineLayout(device, &pipeline_layout_ci, nullptr,
                             &out.pipeline_layout) != VK_SUCCESS) {
    fail("vkCreatePipelineLayout (compute) failed");
  }

  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_size.descriptorCount = 1;

  VkDescriptorPoolCreateInfo pool_ci{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pool_ci.maxSets = 1;
  pool_ci.poolSizeCount = 1;
  pool_ci.pPoolSizes = &pool_size;
  if (vkCreateDescriptorPool(device, &pool_ci, nullptr, &out.descriptor_pool) !=
      VK_SUCCESS) {
    fail("vkCreateDescriptorPool failed");
  }

  VkDescriptorSetAllocateInfo set_alloc{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  set_alloc.descriptorPool = out.descriptor_pool;
  set_alloc.descriptorSetCount = 1;
  set_alloc.pSetLayouts = &out.descriptor_set_layout;
  if (vkAllocateDescriptorSets(device, &set_alloc, &out.descriptor_set) !=
      VK_SUCCESS) {
    fail("vkAllocateDescriptorSets failed");
  }

  VkDescriptorBufferInfo buffer_info{};
  buffer_info.buffer = storage_buffer;
  buffer_info.offset = 0;
  buffer_info.range = storage_size;

  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = out.descriptor_set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.pBufferInfo = &buffer_info;
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

  const auto shader = read_spirv(shader_dir + "/" + shader_prefix_for_scene(scene) +
                                 ".comp.spv");
  const VkShaderModule shader_module = create_shader_module(device, shader);

  VkPipelineShaderStageCreateInfo stage{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = shader_module;
  stage.pName = "main";

  VkComputePipelineCreateInfo pipeline_ci{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipeline_ci.stage = stage;
  pipeline_ci.layout = out.pipeline_layout;
  if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
                               &out.pipeline) != VK_SUCCESS) {
    fail("vkCreateComputePipelines failed");
  }

  vkDestroyShaderModule(device, shader_module, nullptr);
  return out;
}

BufferWithMemory create_device_local_buffer(VkPhysicalDevice physical,
                                            VkDevice device, VkDeviceSize size,
                                            VkBufferUsageFlags usage) {
  return create_buffer(physical, device, size, usage,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

void destroy_compute_resources(VkDevice device, ComputeResources &resources) {
  if (resources.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, resources.pipeline, nullptr);
  }
  if (resources.pipeline_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, resources.pipeline_layout, nullptr);
  }
  if (resources.descriptor_pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, resources.descriptor_pool, nullptr);
  }
  if (resources.descriptor_set_layout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, resources.descriptor_set_layout,
                                 nullptr);
  }
}

TriangleResources create_triangle_resources(VkPhysicalDevice physical,
                                            VkDevice device, uint32_t width,
                                            uint32_t height,
                                            const std::string &shader_dir,
                                            const std::string &scene) {
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

  create_graphics_pipeline(device, out.render_pass, shader_dir, scene,
                           out.pipeline_layout, out.pipeline);
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

#if VK_BENCH_HAS_WINDOW
VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR> &formats) {
  for (const auto &format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    }
  }
  return formats.front();
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR> &modes,
                                     uint32_t vsync) {
  if (vsync != 0) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }
  for (const auto mode : modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return mode;
    }
  }
  for (const auto mode : modes) {
    if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
      return mode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR &caps, uint32_t width,
                         uint32_t height) {
  if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return caps.currentExtent;
  }
  return {std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
          std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)};
}

WindowResources create_window_resources(VkPhysicalDevice physical, VkDevice device,
                                        VkSurfaceKHR surface, uint32_t width,
                                        uint32_t height, uint32_t vsync,
                                        const std::string &shader_dir,
                                        const std::string &scene) {
  WindowResources out{};
  out.surface = surface;

  VkSurfaceCapabilitiesKHR caps{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps) !=
      VK_SUCCESS) {
    fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
  }

  uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr);
  if (format_count == 0) {
    fail("No swapchain formats available");
  }
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, formats.data());

  uint32_t mode_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, nullptr);
  if (mode_count == 0) {
    fail("No present modes available");
  }
  std::vector<VkPresentModeKHR> modes(mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count,
                                            modes.data());

  const VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
  const VkPresentModeKHR present_mode = choose_present_mode(modes, vsync);
  const VkExtent2D extent = choose_extent(caps, width, height);

  uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) {
    image_count = std::min(image_count, caps.maxImageCount);
  }

  VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = surface;
  sci.minImageCount = image_count;
  sci.imageFormat = surface_format.format;
  sci.imageColorSpace = surface_format.colorSpace;
  sci.imageExtent = extent;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = present_mode;
  sci.clipped = VK_TRUE;

  if (vkCreateSwapchainKHR(device, &sci, nullptr, &out.swapchain) != VK_SUCCESS) {
    fail("vkCreateSwapchainKHR failed");
  }

  uint32_t swap_image_count = 0;
  vkGetSwapchainImagesKHR(device, out.swapchain, &swap_image_count, nullptr);
  out.images.resize(swap_image_count);
  vkGetSwapchainImagesKHR(device, out.swapchain, &swap_image_count,
                          out.images.data());

  out.color_format = surface_format.format;
  out.extent = extent;

  out.views.reserve(out.images.size());
  for (const VkImage image : out.images) {
    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = out.color_format;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &ivci, nullptr, &view) != VK_SUCCESS) {
      fail("vkCreateImageView (swapchain) failed");
    }
    out.views.push_back(view);
  }

  VkAttachmentDescription color_attachment{};
  color_attachment.format = out.color_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_ref{};
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &color_attachment;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &subpass;
  rpci.dependencyCount = 1;
  rpci.pDependencies = &dependency;

  if (vkCreateRenderPass(device, &rpci, nullptr, &out.render_pass) != VK_SUCCESS) {
    fail("vkCreateRenderPass (window) failed");
  }

  out.framebuffers.reserve(out.views.size());
  for (const VkImageView view : out.views) {
    VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass = out.render_pass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &view;
    fbci.width = out.extent.width;
    fbci.height = out.extent.height;
    fbci.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &framebuffer) != VK_SUCCESS) {
      fail("vkCreateFramebuffer (window) failed");
    }
    out.framebuffers.push_back(framebuffer);
  }

  create_graphics_pipeline(device, out.render_pass, shader_dir, scene,
                           out.pipeline_layout, out.pipeline);
  return out;
}

void destroy_window_resources(VkDevice device, VkInstance instance,
                              WindowResources &resources) {
  if (resources.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, resources.pipeline, nullptr);
  }
  if (resources.pipeline_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, resources.pipeline_layout, nullptr);
  }
  for (VkFramebuffer fb : resources.framebuffers) {
    vkDestroyFramebuffer(device, fb, nullptr);
  }
  if (resources.render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, resources.render_pass, nullptr);
  }
  for (VkImageView view : resources.views) {
    vkDestroyImageView(device, view, nullptr);
  }
  if (resources.swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, resources.swapchain, nullptr);
  }
  if (resources.surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, resources.surface, nullptr);
  }
  if (resources.window != nullptr) {
    glfwDestroyWindow(resources.window);
    resources.window = nullptr;
  }
}
#endif

int main(int argc, char **argv) {
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkQueryPool query_pool = VK_NULL_HANDLE;
  VkFence headless_frame_fence = VK_NULL_HANDLE;
  TriangleResources triangle{};
  ComputeResources compute{};
  BufferWithMemory compute_buffer{};
  std::optional<CaptureTarget> screenshot_target;
  bool glfw_initialized = false;
#if VK_BENCH_HAS_WINDOW
  WindowResources window_resources{};
  VkSemaphore image_available = VK_NULL_HANDLE;
  VkSemaphore render_finished = VK_NULL_HANDLE;
  VkFence frame_fence = VK_NULL_HANDLE;
#endif
  auto cleanup = [&]() {
    if (device != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device);
      destroy_compute_resources(device, compute);
      destroy_buffer(device, compute_buffer);
      destroy_triangle_resources(device, triangle);
      if (query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, query_pool, nullptr);
        query_pool = VK_NULL_HANDLE;
      }
      if (headless_frame_fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, headless_frame_fence, nullptr);
        headless_frame_fence = VK_NULL_HANDLE;
      }
      if (pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
      }
#if VK_BENCH_HAS_WINDOW
      if (image_available != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, image_available, nullptr);
        image_available = VK_NULL_HANDLE;
      }
      if (render_finished != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, render_finished, nullptr);
        render_finished = VK_NULL_HANDLE;
      }
      if (frame_fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, frame_fence, nullptr);
        frame_fence = VK_NULL_HANDLE;
      }
      destroy_window_resources(device, instance, window_resources);
#endif
      vkDestroyDevice(device, nullptr);
      device = VK_NULL_HANDLE;
    }
#if VK_BENCH_HAS_WINDOW
    if (glfw_initialized) {
      glfwTerminate();
      glfw_initialized = false;
    }
#endif
    if (instance != VK_NULL_HANDLE) {
      vkDestroyInstance(instance, nullptr);
      instance = VK_NULL_HANDLE;
    }
  };

  try {
    const Config cfg = parse_args(argc, argv);
    const bool windowed = !cfg.headless;
    const bool graphics_scene = scene_uses_graphics(cfg.scene);
    const bool compute_scene = scene_uses_compute(cfg.scene);

#if !VK_BENCH_HAS_WINDOW
    if (windowed) {
      fail("Window mode requested but glfw3 is not available in this build. "
           "Rebuild with glfw3 or pass --headless.");
    }
#endif

#if VK_BENCH_HAS_WINDOW
    if (windowed) {
      if (!graphics_scene) {
        fail("Window mode is only supported for graphics scenes");
      }
      if (!glfwInit()) {
        fail("glfwInit failed");
      }
      glfw_initialized = true;
      glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
      glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
      window_resources.window = glfwCreateWindow(
          static_cast<int>(cfg.width), static_cast<int>(cfg.height), "vk-bench",
          nullptr, nullptr);
      if (window_resources.window == nullptr) {
        fail("glfwCreateWindow failed");
      }
    }
#endif

    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "vk-bench";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
    app_info.pEngineName = "none";
    app_info.engineVersion = VK_MAKE_VERSION(0, 3, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char *> instance_extensions;
#if VK_BENCH_HAS_WINDOW
    if (windowed) {
      uint32_t ext_count = 0;
      const char **exts = glfwGetRequiredInstanceExtensions(&ext_count);
      if (exts == nullptr || ext_count == 0) {
        fail("glfwGetRequiredInstanceExtensions failed");
      }
      instance_extensions.assign(exts, exts + ext_count);
    }
#endif

    VkInstanceCreateInfo instance_ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_ci.pApplicationInfo = &app_info;
    instance_ci.enabledExtensionCount =
        static_cast<uint32_t>(instance_extensions.size());
    instance_ci.ppEnabledExtensionNames = instance_extensions.data();

    if (vkCreateInstance(&instance_ci, nullptr, &instance) != VK_SUCCESS) {
      fail("vkCreateInstance failed");
    }

#if VK_BENCH_HAS_WINDOW
    if (windowed) {
      if (glfwCreateWindowSurface(instance, window_resources.window, nullptr,
                                  &window_resources.surface) != VK_SUCCESS) {
        fail("glfwCreateWindowSurface failed");
      }
    }
#endif

    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
    if (gpu_count == 0) {
      fail("No Vulkan devices found");
    }
    std::vector<VkPhysicalDevice> gpus(gpu_count);
    vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data());

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queue_index = 0;
    for (const auto gpu : gpus) {
      uint32_t queue_family_count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count,
                                               nullptr);
      std::vector<VkQueueFamilyProperties> queue_props(queue_family_count);
      vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count,
                                               queue_props.data());
      for (uint32_t i = 0; i < queue_family_count; ++i) {
        const bool supports_graphics =
            (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool supports_compute =
            (queue_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        const bool supports_timestamps = queue_props[i].timestampValidBits > 0;
        if ((graphics_scene && !supports_graphics) ||
            (compute_scene && !supports_compute) || !supports_timestamps) {
          continue;
        }
        bool ok = true;
#if VK_BENCH_HAS_WINDOW
        if (windowed) {
          VkBool32 present_supported = VK_FALSE;
          vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, window_resources.surface,
                                               &present_supported);
          ok = present_supported == VK_TRUE;
        }
#endif
        if (ok) {
          physical = gpu;
          queue_index = i;
          break;
        }
      }
      if (physical != VK_NULL_HANDLE) {
        break;
      }
    }
    if (physical == VK_NULL_HANDLE) {
      fail("No compatible queue family found");
    }

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);

    std::vector<const char *> device_extensions;
#if VK_BENCH_HAS_WINDOW
    if (windowed) {
      device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
#endif

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_index;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    dci.ppEnabledExtensionNames = device_extensions.data();

    if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS) {
      fail("vkCreateDevice failed");
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_index, 0, &queue);

    VkCommandPoolCreateInfo pool_ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_ci.queueFamilyIndex = queue_index;
    if (vkCreateCommandPool(device, &pool_ci, nullptr, &pool) != VK_SUCCESS) {
      fail("vkCreateCommandPool failed");
    }

    VkCommandBufferAllocateInfo alloc{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &alloc, &cmd) != VK_SUCCESS) {
      fail("vkAllocateCommandBuffers failed");
    }

    // Two timestamps per frame: start/end of the GPU workload region.
    VkQueryPoolCreateInfo query_ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    query_ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_ci.queryCount = 2;
    if (vkCreateQueryPool(device, &query_ci, nullptr, &query_pool) !=
        VK_SUCCESS) {
      fail("vkCreateQueryPool failed");
    }

    if (!windowed) {
      VkFenceCreateInfo fence_ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      if (vkCreateFence(device, &fence_ci, nullptr, &headless_frame_fence) !=
          VK_SUCCESS) {
        fail("vkCreateFence (headless frame fence) failed");
      }
    }

    const std::string shader_dir =
        (graphics_scene || compute_scene) ? pick_shader_dir(argv[0], cfg.scene)
                                          : std::string();
    if (graphics_scene) {
#if VK_BENCH_HAS_WINDOW
      if (windowed) {
        const WindowResources created = create_window_resources(
            physical, device, window_resources.surface, cfg.width, cfg.height,
            cfg.vsync, shader_dir, cfg.scene);
        window_resources.swapchain = created.swapchain;
        window_resources.color_format = created.color_format;
        window_resources.extent = created.extent;
        window_resources.images = created.images;
        window_resources.views = created.views;
        window_resources.render_pass = created.render_pass;
        window_resources.framebuffers = created.framebuffers;
        window_resources.pipeline_layout = created.pipeline_layout;
        window_resources.pipeline = created.pipeline;

        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(device, &sci, nullptr, &image_available) !=
            VK_SUCCESS) {
          fail("vkCreateSemaphore (image_available) failed");
        }
        if (vkCreateSemaphore(device, &sci, nullptr, &render_finished) !=
            VK_SUCCESS) {
          fail("vkCreateSemaphore (render_finished) failed");
        }
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(device, &fci, nullptr, &frame_fence) != VK_SUCCESS) {
          fail("vkCreateFence (window frame fence) failed");
        }
      } else
#endif
      {
        triangle = create_triangle_resources(physical, device, cfg.width,
                                             cfg.height, shader_dir, cfg.scene);
        screenshot_target = CaptureTarget{triangle.color_image,
                                          VK_FORMAT_R8G8B8A8_UNORM, cfg.width,
                                          cfg.height,
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
      }
    }

    if (compute_scene) {
      compute_buffer = create_device_local_buffer(
          physical, device, kComputeBufferSize,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
              VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      compute = create_compute_resources(device, shader_dir, cfg.scene,
                                         compute_buffer.buffer,
                                         kComputeBufferSize);
    }

    std::vector<double> gpu_ms;
    std::vector<double> cpu_ms;
    auto run_frame = [&](bool record) -> bool {
#if VK_BENCH_HAS_WINDOW
      if (windowed) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window_resources.window)) {
          return false;
        }
      }
#endif
      vkResetCommandBuffer(cmd, 0);
      VkCommandBufferBeginInfo begin{
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        fail("vkBeginCommandBuffer failed");
      }
      vkCmdResetQueryPool(cmd, query_pool, 0, 2);
      // Timestamp #0: before any rendering or dispatch commands for this frame.
      vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 0);

      uint32_t image_index = 0;
#if VK_BENCH_HAS_WINDOW
      if (windowed) {
        vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &frame_fence);
        if (vkAcquireNextImageKHR(device, window_resources.swapchain, UINT64_MAX,
                                  image_available, VK_NULL_HANDLE,
                                  &image_index) != VK_SUCCESS) {
          fail("vkAcquireNextImageKHR failed");
        }
      }
#endif

      if (graphics_scene) {
        VkClearValue clear{};
        clear.color.float32[0] = 0.02f;
        clear.color.float32[1] = 0.02f;
        clear.color.float32[2] = 0.02f;
        clear.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
#if VK_BENCH_HAS_WINDOW
        if (windowed) {
          rpbi.renderPass = window_resources.render_pass;
          rpbi.framebuffer = window_resources.framebuffers[image_index];
          rpbi.renderArea.extent = window_resources.extent;
        } else
#endif
        {
          rpbi.renderPass = triangle.render_pass;
          rpbi.framebuffer = triangle.framebuffer;
          rpbi.renderArea.extent = {cfg.width, cfg.height};
        }
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

#if VK_BENCH_HAS_WINDOW
        if (windowed) {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            window_resources.pipeline);
        } else
#endif
        {
          vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            triangle.pipeline);
        }
        VkViewport viewport{};
#if VK_BENCH_HAS_WINDOW
        if (windowed) {
          viewport.width = static_cast<float>(window_resources.extent.width);
          viewport.height = static_cast<float>(window_resources.extent.height);
        } else
#endif
        {
          viewport.width = static_cast<float>(cfg.width);
          viewport.height = static_cast<float>(cfg.height);
        }
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
#if VK_BENCH_HAS_WINDOW
        if (windowed) {
          scissor.extent = window_resources.extent;
        } else
#endif
        {
          scissor.extent = {cfg.width, cfg.height};
        }
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        const uint32_t triangle_count =
            (cfg.scene == "triangle") ? cfg.triangles : kMillionTrisTriangleCount;
        // Vertex shader derives positions from vertex/instance ids, so no VBO is needed.
        vkCmdDraw(cmd, 3, triangle_count, 0, 0);
        vkCmdEndRenderPass(cmd);
      } else {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                compute.pipeline_layout, 0, 1,
                                &compute.descriptor_set, 0, nullptr);

        const uint32_t frame_seed = record ? cfg.warmup + static_cast<uint32_t>(cpu_ms.size())
                                           : 0u;
        vkCmdPushConstants(cmd, compute.pipeline_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(frame_seed), &frame_seed);

        const uint32_t dispatch_count = static_cast<uint32_t>(
            kComputeBufferSize / (sizeof(uint32_t) * kComputeLocalSize));
        vkCmdDispatch(cmd, dispatch_count, 1, 1);
      }

      // Timestamp #1: after all workload commands in this command buffer.
      vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 1);
      if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        fail("vkEndCommandBuffer failed");
      }

      const auto cpu_start = std::chrono::high_resolution_clock::now();
#if VK_BENCH_HAS_WINDOW
      if (windowed) {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished;
        if (vkQueueSubmit(queue, 1, &submit, frame_fence) != VK_SUCCESS) {
          fail("vkQueueSubmit (windowed) failed");
        }
      } else
#endif
      {
        // Reuse a single fence to avoid per-frame creation jitter in CPU timings.
        if (vkResetFences(device, 1, &headless_frame_fence) != VK_SUCCESS) {
          fail("vkResetFences (headless) failed");
        }
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        if (vkQueueSubmit(queue, 1, &submit, headless_frame_fence) !=
            VK_SUCCESS) {
          fail("vkQueueSubmit (headless) failed");
        }
        if (vkWaitForFences(device, 1, &headless_frame_fence, VK_TRUE,
                            UINT64_MAX) != VK_SUCCESS) {
          fail("vkWaitForFences (headless) failed");
        }
      }
      const auto cpu_end = std::chrono::high_resolution_clock::now();

#if VK_BENCH_HAS_WINDOW
      if (windowed) {
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &window_resources.swapchain;
        present.pImageIndices = &image_index;
        if (vkQueuePresentKHR(queue, &present) != VK_SUCCESS) {
          fail("vkQueuePresentKHR failed");
        }
        vkWaitForFences(device, 1, &frame_fence, VK_TRUE, UINT64_MAX);
      }
#endif

      uint64_t timestamps[2] = {0, 0};
      if (vkGetQueryPoolResults(device, query_pool, 0, 2, sizeof(timestamps),
                                timestamps, sizeof(uint64_t),
                                VK_QUERY_RESULT_64_BIT |
                                    VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) {
        fail("vkGetQueryPoolResults failed");
      }
      if (record) {
        // timestampPeriod is in nanoseconds; convert delta to milliseconds.
        cpu_ms.push_back(std::chrono::duration<double, std::milli>(cpu_end - cpu_start)
                             .count());
        gpu_ms.push_back(static_cast<double>(timestamps[1] - timestamps[0]) *
                         props.limits.timestampPeriod * 1e-6);
      }
      return true;
    };

    for (uint32_t i = 0; i < cfg.warmup; ++i) {
      if (!run_frame(false)) {
        break;
      }
    }
    for (uint32_t i = 0; i < cfg.frames; ++i) {
      if (!run_frame(true)) {
        break;
      }
    }
    if (cpu_ms.empty()) {
      fail("No frames recorded");
    }

    const Stats cpu = summarize(cpu_ms);
    const Stats gpu = summarize(gpu_ms);

    if (screenshot_target.has_value()) {
      const auto screenshot_path = screenshot_path_for_output(cfg.out);
      capture_image_to_bmp(physical, device, queue, pool, *screenshot_target,
                           screenshot_path);
      std::cout << "Wrote screenshot to " << screenshot_path.string() << "\n";
    }

    std::ofstream out(cfg.out);
    if (!out) {
      fail("Failed to open output file: " + cfg.out);
    }
    out << std::fixed << std::setprecision(4);
    out << "{\n";
    out << "  \"scene\": \"" << cfg.scene << "\",\n";
    out << "  \"headless\": " << (cfg.headless ? "true" : "false") << ",\n";
    out << "  \"frames\": " << static_cast<uint32_t>(cpu_ms.size()) << ",\n";
    out << "  \"warmup\": " << cfg.warmup << ",\n";
    out << "  \"vsync\": " << cfg.vsync << ",\n";
    if (cfg.scene == "triangle") {
      out << "  \"triangle_count\": " << cfg.triangles << ",\n";
    } else if (cfg.scene == "million-tris") {
      out << "  \"triangle_count\": " << kMillionTrisTriangleCount << ",\n";
    }
#if VK_BENCH_HAS_WINDOW
    if (windowed && graphics_scene) {
      out << "  \"resolution\": \"" << window_resources.extent.width << "x"
          << window_resources.extent.height << "\",\n";
    } else
#endif
    {
      out << "  \"resolution\": \"" << cfg.width << "x" << cfg.height << "\",\n";
    }
    out << "  \"device_name\": \"" << escape_json_string(props.deviceName)
        << "\",\n";
    out << "  \"driver_version\": " << props.driverVersion << ",\n";
    out << "  \"cpu_frame_time_ms\": {\"avg\": " << cpu.avg
        << ", \"p50\": " << cpu.p50 << ", \"p95\": " << cpu.p95 << "},\n";
    out << "  \"gpu_frame_time_ms\": {\"avg\": " << gpu.avg
        << ", \"p50\": " << gpu.p50 << ", \"p95\": " << gpu.p95 << "}\n";
    out << "}\n";
    std::cout << "Wrote benchmark results to " << cfg.out << "\n";

    cleanup();
    return 0;
  } catch (const std::exception &e) {
    cleanup();
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
