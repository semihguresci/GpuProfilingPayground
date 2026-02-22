#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

[[noreturn]] void fail(const std::string &msg) {
  throw std::runtime_error(msg);
}

Stats summarize(std::vector<double> values) {
  if (values.empty())
    return {};
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
      if (i + 1 >= argc)
        fail("Missing value for " + name);
      return argv[++i];
    };

    if (arg == "--headless")
      cfg.headless = true;
    else if (arg == "--frames")
      cfg.frames = std::stoul(read("--frames"));
    else if (arg == "--warmup")
      cfg.warmup = std::stoul(read("--warmup"));
    else if (arg == "--vsync")
      cfg.vsync = std::stoul(read("--vsync"));
    else if (arg == "--out")
      cfg.out = read("--out");
    else if (arg == "--scene")
      cfg.scene = read("--scene");
    else if (arg == "--resolution") {
      const auto value = read("--resolution");
      const auto x = value.find('x');
      if (x == std::string::npos)
        fail("Expected WIDTHxHEIGHT for --resolution");
      cfg.width = std::stoul(value.substr(0, x));
      cfg.height = std::stoul(value.substr(x + 1));
    } else if (arg == "--help" || arg == "-h") {
      std::cout
          << "vk-bench --headless --frames N --warmup N --vsync 0|1 --out "
             "file.json --scene triangle|million-tris|compute-copy\n";
      std::exit(0);
    } else
      fail("Unknown argument: " + arg);
  }
  return cfg;
}

uint32_t scene_iterations(const std::string &scene) {
  if (scene == "triangle")
    return 64;
  if (scene == "million-tris")
    return 4096;
  if (scene == "compute-copy")
    return 16384;
  fail("Unknown scene: " + scene);
}

int main(int argc, char **argv) {
  try {
    const Config cfg = parse_args(argc, argv);

    VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "vk-bench";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "none";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instance_ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_ci.pApplicationInfo = &app_info;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&instance_ci, nullptr, &instance) != VK_SUCCESS)
      fail("vkCreateInstance failed");

    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
    if (gpu_count == 0)
      fail("No Vulkan devices found");
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
    if (!queue_index.has_value())
      fail("No graphics queue family found");

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = *queue_index;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS)
      fail("vkCreateDevice failed");

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

    const VkDeviceSize buffer_size = 64ULL * 1024ULL * 1024ULL;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = buffer_size;
    bci.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer src = VK_NULL_HANDLE;
    VkBuffer dst = VK_NULL_HANDLE;
    vkCreateBuffer(device, &bci, nullptr, &src);
    vkCreateBuffer(device, &bci, nullptr, &dst);

    const uint32_t iterations = scene_iterations(cfg.scene);
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

      VkBufferCopy copy{};
      copy.size = 4ULL * 1024ULL * 1024ULL;
      for (uint32_t i = 0; i < iterations; ++i)
        vkCmdCopyBuffer(cmd, src, dst, 1, &copy);

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

    for (uint32_t i = 0; i < cfg.warmup; ++i)
      run_frame(false);
    for (uint32_t i = 0; i < cfg.frames; ++i)
      run_frame(true);

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
    out.close();

    std::cout << "Wrote benchmark results to " << cfg.out << "\n";

    vkDestroyBuffer(device, src, nullptr);
    vkDestroyBuffer(device, dst, nullptr);
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
