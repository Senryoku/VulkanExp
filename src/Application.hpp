#pragma once

#include <chrono>
#include <optional>
#include <set>
#include <stdexcept>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <fmt/color.h>
#include <fmt/core.h>

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <FileWatch.hpp>

#include "Logger.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/DescriptorPool.hpp"
#include "vulkan/DescriptorSetLayout.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/DeviceMemory.hpp"
#include "vulkan/Fence.hpp"
#include "vulkan/Framebuffer.hpp"
#include "vulkan/ImageView.hpp"
#include "vulkan/Mesh.hpp"
#include "vulkan/Pipeline.hpp"
#include "vulkan/RenderPass.hpp"
#include "vulkan/Semaphore.hpp"
#include "vulkan/Shader.hpp"
#include "vulkan/Vertex.hpp"
#include <vulkan/Image.hpp>

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                             VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if(func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if(func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

class Application {
  public:
    void run() {
        _mesh.loadOBJ("data/models/lucy.obj");
        _mesh.normalizeVertices();
        _mesh.computeVertexNormals();

        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

  private:
    const uint32_t InitialWidth = 1280;
    const uint32_t InitialHeight = 800;

    const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

    const std::vector<const char*> requiredDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
    const bool _enableValidationLayers = false;
#else
    const bool _enableValidationLayers = true;
#endif

    bool _dirtyShaders = true; // Re-compile on startup
    // Auto re-compile shaders
    filewatch::FileWatch<std::string> _shadersFileWatcher{"./src/shaders/", [&](const std::string& file, const filewatch::Event event_type) {
                                                              _dirtyShaders = true;
    }};

    GLFWwindow* _window = nullptr;
    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debugMessenger;
    PhysicalDevice _physicalDevice;
    Device _device;
    VkSurfaceKHR _surface;
    VkQueue _graphicsQueue;
    VkQueue _presentQueue;

    VkSwapchainKHR _swapChain;
    std::vector<VkImage> _swapChainImages;
    VkFormat _swapChainImageFormat;
    VkExtent2D _swapChainExtent;
    std::vector<ImageView> _swapChainImageViews;

    VkFormat _depthFormat;
    Image _depthImage;
    DeviceMemory _depthImageMemory;
    ImageView _depthImageView;

    RenderPass _renderPass;
    DescriptorSetLayout _descriptorSetLayout;
    Pipeline _pipeline;
    std::vector<Framebuffer> _swapChainFramebuffers;
    CommandPool _commandPool;
    CommandPool _tempCommandPool;
    CommandBuffers _commandBuffers;
    std::vector<Semaphore> _renderFinishedSemaphore;
    std::vector<Semaphore> _imageAvailableSemaphore;
    std::vector<Fence> _inFlightFences;
    std::vector<VkFence> _imagesInFlight;

    std::vector<Buffer> _uniformBuffers;
    DeviceMemory _uniformBuffersMemory;

    DescriptorPool _descriptorPool;

    VkDescriptorPool _imguiDescriptorPool;
    std::vector<Framebuffer> _imguiFramebuffers;
    RenderPass _imguiRenderPass;
    CommandPool _imguiCommandPool;
    CommandBuffers _imguiCommandBuffers;

    Mesh _mesh;
    DeviceMemory _deviceMemory;

    bool _framebufferResized = false;

    // See https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPresentModeKHR.html
    // TLDR (as I understand it:) ):
    // VK_PRESENT_MODE_FIFO_KHR to limit the frame rate to the display refresh rate (basically VSync, increases input lag)
    // VK_PRESENT_MODE_MAILBOX_KHR to allow submiting gpu work more frequently, while presenting only on display refresh
    VkPresentModeKHR _preferedPresentMode = VK_PRESENT_MODE_FIFO_KHR; // VK_PRESENT_MODE_MAILBOX_KHR

    const int MAX_FRAMES_IN_FLIGHT = 2;
    size_t _currentFrame = 0;

    double _cameraZoom = 600.0;

    bool _moving = false;
    double _last_xpos = 0, _last_ypos = 0;

    void createSwapChain();
    void initSwapChain();
    void recreateSwapChain();
    void cleanupSwapChain();

    void compileShaders() {
        // Could use "start" to launch it asynchronously, but I'm not sure if there's a way to react to the command finishing
        // Could use popen() instead of system() to capture the output too.
        system("powershell.exe -ExecutionPolicy RemoteSigned .\\compile_shaders.ps1");
        // Fixme: We can probably do a lot less :) (Like only recreating the pipeline, which could even be done in another thread)
        recreateSwapChain();
    }

    void initWindow() {
        fmt::print("Window initialisation... ");

        if(!glfwInit())
            error("Error intialising GLFW. ");

        glfwWindowHint(GLFW_CLIENT_API,
                       GLFW_NO_API); // Opt-out of creating an OpenGL Context
        // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, 1);
        //glfwWindowHint(GLFW_DECORATED, false);

        _window = glfwCreateWindow(InitialWidth, InitialHeight, "VulkanExp", nullptr, nullptr);
        if(_window == nullptr)
            error("Error while creating GLFW Window. ");

        glfwSetWindowUserPointer(_window, this); // Allow access to our Application instance in callbacks
        glfwSetFramebufferSizeCallback(_window, framebufferResizeCallback);
        glfwSetMouseButtonCallback(_window, mouse_button_callback);

        glfwSetScrollCallback(_window, scroll_callback);

        success("Done.\n");
    }

    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        app->_cameraZoom -= 5.0 * yoffset;
    };

    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
        if(ImGui::GetIO().WantCaptureMouse)
            return;
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if(button == GLFW_MOUSE_BUTTON_LEFT) {
            app->_moving = action == GLFW_PRESS;
            glfwGetCursorPos(window, &app->_last_xpos, &app->_last_ypos);
        }
    }

    bool checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        for(const char* layerName : validationLayers) {
            bool layerFound = false;

            for(const auto& layerProperties : availableLayers) {
                if(strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if(!layerFound) {
                error("Validation Layer '{}' not found.\n", layerName);
                return false;
            }
        }

        return true;
    }

    void createInstance() {
        VkApplicationInfo appInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = "VulkanExp",
            .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .pEngineName = "Lilia",
            .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
            .apiVersion = VK_API_VERSION_1_2,
        };

        if(_enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers requested, but not available!");
        }

        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
        fmt::print("Available vulkan extensions ({}):\n", extensionCount);
        for(uint32_t i = 0; i < extensionCount; ++i)
            fmt::print("\t{}\n", extensions[i].extensionName);

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> requestedExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if(_enableValidationLayers)
            requestedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = _enableValidationLayers ? &DebugMessengerCreateInfo : nullptr,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = _enableValidationLayers ? static_cast<uint32_t>(validationLayers.size()) : 0,
            .ppEnabledLayerNames = _enableValidationLayers ? validationLayers.data() : nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
            .ppEnabledExtensionNames = requestedExtensions.data(),
        };

        auto result = vkCreateInstance(&createInfo, nullptr, &_instance);
        if(result != VK_SUCCESS) {
            throw std::runtime_error(fmt::format("Failed to create Vulkan Instance (Error: {}).", result));
        } else {
            fmt::print("Created Vulkan Instance with {} extensions:\n", glfwExtensionCount);
            for(uint32_t i = 0; i < glfwExtensionCount; ++i)
                fmt::print("\t{}\n", glfwExtensions[i]);
        }
    }

    void setupDebugMessenger() {
        if(!_enableValidationLayers) return;
        if(CreateDebugUtilsMessengerEXT(_instance, &DebugMessengerCreateInfo, nullptr, &_debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("Failed to set up debug messenger!");
        }
    }

    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end());

        for(const auto& extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    unsigned int rateDevice(PhysicalDevice device) const {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

        // Required Capabilities

        PhysicalDevice::QueueFamilyIndices indices = device.getQueues(_surface);
        if(!indices.graphicsFamily.has_value())
            return 0;
        if(!checkDeviceExtensionSupport(device))
            return 0;

        auto swapChainSupport = device.getSwapChainSupport(_surface);
        if(swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty())
            return 0;

        // Optional Capabilities
        int score = 1;
        if(deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 100;

        return score;
    }

    void createSurface() {
        if(glfwCreateWindowSurface(_instance, _window, nullptr, &_surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface!");
        }
    }

    PhysicalDevice pickPhysicalDevice() const {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
        if(deviceCount == 0) {
            throw std::runtime_error("Failed to find GPUs with Vulkan support!");
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

        PhysicalDevice physicalDevice;
        int maxScore = 0;
        for(const auto& device : devices) {
            auto pd = PhysicalDevice(device);
            auto score = rateDevice(pd);
            if(score > maxScore) {
                physicalDevice = pd;
                maxScore = score;
                break;
            }
        }
        return physicalDevice;
    }

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for(const auto& availableFormat : availableFormats) {
            // FIXME: Imgui windows don't look right when VK_COLOR_SPACE_SRGB_NONLINEAR_KHR is used.
            //if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        for(const auto& availablePresentMode : availablePresentModes) {
            if(availablePresentMode == _preferedPresentMode) {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if(capabilities.currentExtent.width != UINT32_MAX) {
            return capabilities.currentExtent;
        } else {
            int width, height;
            glfwGetFramebufferSize(_window, &width, &height);

            VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function) {
        CommandPool tempCommandPool;
        tempCommandPool.create(_device, _physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
        CommandBuffers buffers;
        buffers.allocate(_device, _commandPool, 1);
        buffers[0].begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        function(buffers[0]);

        buffers[0].end();
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = buffers.getBuffersHandles().data(),
        };
        vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(_graphicsQueue);
        buffers.free();
        tempCommandPool.destroy();
    }

    void initImGui(uint32_t queueFamily);

    void initVulkan() {
        fmt::print("Vulkan initialisation... ");

        if(!glfwVulkanSupported()) {
            error("GLFW: Vulkan Not Supported\n");
            return;
        }

        createInstance();
        setupDebugMessenger();
        createSurface();
        auto physicalDevice = pickPhysicalDevice();
        if(!physicalDevice.isValid())
            throw std::runtime_error("Failed to find a suitable GPU!");
        _physicalDevice = physicalDevice;
        _device = Device{_surface, _physicalDevice, requiredDeviceExtensions};
        auto queueIndices = _physicalDevice.getQueues(_surface);
        vkGetDeviceQueue(_device, queueIndices.graphicsFamily.value(), 0, &_graphicsQueue);
        vkGetDeviceQueue(_device, queueIndices.presentFamily.value(), 0, &_presentQueue);

        createSwapChain(); 
        _commandPool.create(_device, queueIndices.graphicsFamily.value());
        _imguiCommandPool.create(_device, queueIndices.graphicsFamily.value(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        _tempCommandPool.create(_device, queueIndices.graphicsFamily.value(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

        auto vertexDataSize = _mesh.getVertexByteSize();

        Buffer stagingBuffer;
        DeviceMemory stagingMemory;
        stagingBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexDataSize);
        auto stagingBufferMemReq = stagingBuffer.getMemoryRequirements();
        stagingMemory.allocate(_device,
                               _physicalDevice.findMemoryType(stagingBufferMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                               stagingBufferMemReq.size);
        vkBindBufferMemory(_device, stagingBuffer, stagingMemory, 0);

        _mesh.init(_device);
        auto vertexBufferMemReq = _mesh.getVertexBuffer().getMemoryRequirements();
        auto indexBufferMemReq = _mesh.getIndexBuffer().getMemoryRequirements();
        _deviceMemory.allocate(_device, _physicalDevice.findMemoryType(vertexBufferMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
                               vertexBufferMemReq.size + indexBufferMemReq.size);
        vkBindBufferMemory(_device, _mesh.getVertexBuffer(), _deviceMemory, 0);
        vkBindBufferMemory(_device, _mesh.getIndexBuffer(), _deviceMemory, vertexBufferMemReq.size);
        _mesh.upload(_device, stagingBuffer, stagingMemory, _tempCommandPool, _graphicsQueue);

        initSwapChain();

        _renderFinishedSemaphore.resize(MAX_FRAMES_IN_FLIGHT);
        _imageAvailableSemaphore.resize(MAX_FRAMES_IN_FLIGHT);
        for(auto& s : _renderFinishedSemaphore)
            s.create(_device);
        for(auto& s : _imageAvailableSemaphore)
            s.create(_device);
        _inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        for(auto& f : _inFlightFences)
            f.create(_device);

        initImGui(queueIndices.graphicsFamily.value());

        success("Done.\n");
    }

    void drawUI();

    void mainLoop() {
        while(!glfwWindowShouldClose(_window)) {
            glfwPollEvents();

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
            drawUI();
            ImGui::Render();
            // Update and Render additional Platform Windows
            if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            if(_dirtyShaders) {
                compileShaders();
                _dirtyShaders = false;
            }

            drawFrame();
        }
        vkDeviceWaitIdle(_device);
    }

    void drawFrame();

    void updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo{};

        if(_moving) {
            double xpos, ypos;
            glfwGetCursorPos(_window, &xpos, &ypos);
            float dx = 2 * 3.14159 * (_last_xpos - xpos) / _swapChainExtent.width, dy = 3.14159 * (_last_ypos - ypos) / _swapChainExtent.height;
            static glm::vec4 camera_position{_cameraZoom, _cameraZoom, _cameraZoom, 1.f};
            _last_xpos = xpos;
            _last_ypos = ypos;
            camera_position = glm::rotate(glm::mat4{1.0f}, dx, glm::vec3(0.0f, 0.0f, 1.0f)) * camera_position;
            camera_position = glm::rotate(glm::mat4{1.0f}, dy, glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3{camera_position})) * camera_position;
            ubo.model = glm::mat4(1.0f);
            ubo.view = glm::lookAt(glm::vec3{camera_position}, glm::vec3(0.0f, 0.0f, 300.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        } else {
            ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            ubo.view = glm::lookAt(glm::vec3(_cameraZoom, _cameraZoom, _cameraZoom), glm::vec3(0.0f, 0.0f, 300.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        }

        ubo.proj = glm::perspective(glm::radians(45.0f), _swapChainExtent.width / (float)_swapChainExtent.height, 0.1f, 2000.0f);
        ubo.proj[1][1] *= -1;

        void* data;
        size_t offset = static_cast<size_t>(currentImage) * 256; // FIXME: 256 is the alignment (> sizeof(ubo)), should be correctly saved somewhere
        vkMapMemory(_device, _uniformBuffersMemory, offset, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(_device, _uniformBuffersMemory);
    }

    void cleanup() {
        vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        for(auto& f : _inFlightFences)
            f.destroy();
        for(auto& s : _renderFinishedSemaphore)
            s.destroy();
        for(auto& s : _imageAvailableSemaphore)
            s.destroy();

        cleanupSwapChain();
        _commandPool.destroy();
        _imguiCommandPool.destroy();
        _tempCommandPool.destroy();
        _mesh.destroy();
        _deviceMemory.free();

        _device.destroy();
        if(_enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
        }
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyInstance(_instance, nullptr);
        glfwDestroyWindow(_window);
        glfwTerminate();
    }

    static const VkDebugUtilsMessageSeverityFlagBitsEXT ValidationLayerDebugLevel = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {

        if(messageSeverity >= ValidationLayerDebugLevel) {
            switch(messageSeverity) {
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                fmt::print("Validation layer: {}\n", pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                warn("Validation layer: {}\n", pCallbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                error("Validation layer: {}\n", pCallbackData->pMessage);
                break;
            }
        }

        return VK_FALSE;
    }

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        app->_framebufferResized = true;
    }

    const VkDebugUtilsMessengerCreateInfoEXT DebugMessengerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
        .pUserData = nullptr,
    };
};