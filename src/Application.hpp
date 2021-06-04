#pragma once

#include <optional>
#include <set>
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <fmt/color.h>
#include <fmt/core.h>

#include "Logger.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/Device.hpp"
#include "vulkan/DeviceMemory.hpp"
#include "vulkan/Fence.hpp"
#include "vulkan/Framebuffer.hpp"
#include "vulkan/ImageView.hpp"
#include "vulkan/Pipeline.hpp"
#include "vulkan/RenderPass.hpp"
#include "vulkan/Semaphore.hpp"
#include "vulkan/Shader.hpp"
#include "vulkan/Vertex.hpp"

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                      VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if(func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if(func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

class Application {
  public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

  private:
    const uint32_t Width = 800;
    const uint32_t Height = 600;

    const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

    const std::vector<const char*> requiredDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
    const bool _enableValidationLayers = false;
#else
    const bool _enableValidationLayers = true;
#endif

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
    RenderPass _renderPass;
    Pipeline _pipeline;
    std::vector<Framebuffer> _swapChainFramebuffers;
    CommandPool _commandPool;
    CommandBuffers _commandBuffers;
    std::vector<Semaphore> _renderFinishedSemaphore;
    std::vector<Semaphore> _imageAvailableSemaphore;
    std::vector<Fence> _inFlightFences;
    std::vector<VkFence> _imagesInFlight;
    VertexBuffer _vertexBuffer;
    DeviceMemory _vertexBufferMemory;

    bool _framebufferResized = false;

    const int MAX_FRAMES_IN_FLIGHT = 2;

    const std::vector<Vertex> _vertices = {
        {{0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
    };

    void initWindow() {
        fmt::print("Window initialisation... ");

        if(!glfwInit())
            error("Error intialising GLFW. ");

        glfwWindowHint(GLFW_CLIENT_API,
                       GLFW_NO_API); // Opt-out of creating an OpenGL Context
        // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        _window = glfwCreateWindow(Width, Height, "VulkanExp", nullptr, nullptr);
        if(_window == nullptr)
            error("Error while creating GLFW Window. ");

        glfwSetWindowUserPointer(_window, this); // Allow access to our Application instance in callbacks
        glfwSetFramebufferSizeCallback(_window, framebufferResizeCallback);

        success("Done.\n");
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
        if(!_enableValidationLayers)
            return;
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
            if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        for(const auto& availablePresentMode : availablePresentModes) {
            if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
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

    void createSwapChain() {
        auto swapChainSupport = _physicalDevice.getSwapChainSupport(_surface);

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if(swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }
        VkSwapchainCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = _surface,
            .minImageCount = imageCount,
            .imageFormat = surfaceFormat.format,
            .imageColorSpace = surfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform = swapChainSupport.capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };

        auto indices = _physicalDevice.getQueues(_surface);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if(indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;     // Optional
            createInfo.pQueueFamilyIndices = nullptr; // Optional
        }

        if(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapChain) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain.");
        }

        vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, nullptr);
        _swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, _swapChainImages.data());

        _swapChainImageFormat = surfaceFormat.format;
        _swapChainExtent = extent;

        for(size_t i = 0; i < _swapChainImages.size(); i++)
            _swapChainImageViews.push_back(ImageView{_device, _swapChainImages[i], _swapChainImageFormat});
    }

    void initSwapchain() {
        _renderPass.create(_device, _swapChainImageFormat);

        Shader vertShader(_device, "shaders/buffer.vert.spv");
        Shader fragShader(_device, "shaders/triangle.frag.spv");
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
            vertShader.getStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
            fragShader.getStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
        };
        _pipeline.create(_device, shaderStages, _renderPass, _swapChainExtent);

        _swapChainFramebuffers.resize(_swapChainImageViews.size());
        for(size_t i = 0; i < _swapChainImageViews.size(); i++)
            _swapChainFramebuffers[i].create(_device, _renderPass, _swapChainImageViews[i], _swapChainExtent);

        _commandBuffers.allocate(_device, _commandPool, _swapChainFramebuffers.size());

        for(size_t i = 0; i < _commandBuffers.getBuffers().size(); i++) {
            auto b = _commandBuffers.getBuffers()[i];
            b.begin();
            b.beginRenderPass(_renderPass, _swapChainFramebuffers[i], _swapChainExtent);
            _pipeline.bind(b);

            VkBuffer vertexBuffers[] = {_vertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(_commandBuffers.getBuffers()[i], 0, 1, vertexBuffers, offsets);

            b.draw(3);
            b.endRenderPass();
            b.end();
        }

        _imagesInFlight.resize(_swapChainImages.size());
    }

    void initVulkan() {
        fmt::print("Vulkan initialisation... ");

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
        _vertexBuffer.create(_device, sizeof(_vertices[0]) * _vertices.size());
        _vertexBufferMemory.allocate(
            _device,
            _physicalDevice.findMemoryType(_vertexBuffer.getMemoryRequirements().memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
            _vertexBuffer.getMemoryRequirements().size);
        _vertexBufferMemory.fill(_vertices);
        vkBindBufferMemory(_device, _vertexBuffer, _vertexBufferMemory, 0);
        initSwapchain();

        _renderFinishedSemaphore.resize(MAX_FRAMES_IN_FLIGHT);
        _imageAvailableSemaphore.resize(MAX_FRAMES_IN_FLIGHT);
        for(auto& s : _renderFinishedSemaphore)
            s.create(_device);
        for(auto& s : _imageAvailableSemaphore)
            s.create(_device);
        _inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
        for(auto& f : _inFlightFences)
            f.create(_device);

        success("Done.\n");
    }

    void mainLoop() {
        while(!glfwWindowShouldClose(_window)) {
            glfwPollEvents();
            drawFrame();
        }
        vkDeviceWaitIdle(_device);
    }

    size_t _currentFrame = 0;

    void drawFrame() {
        VkFence currentFence = _inFlightFences[_currentFrame];
        vkWaitForFences(_device, 1, &currentFence, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        auto result = vkAcquireNextImageKHR(_device, _swapChain, UINT64_MAX, _imageAvailableSemaphore[_currentFrame], VK_NULL_HANDLE, &imageIndex);

        if(result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return;
        } else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error(fmt::format("Failed to acquire swap chain image. (Error: {})", result));
        }

        // Check if a previous frame is using this image (i.e. there is its
        // fence to wait on)
        if(_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            vkWaitForFences(_device, 1, &_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        }
        // Mark the image as now being in use by this frame
        _imagesInFlight[imageIndex] = currentFence;

        VkSemaphore waitSemaphores[] = {_imageAvailableSemaphore[_currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signalSemaphores[] = {_renderFinishedSemaphore[_currentFrame]};
        auto commandBuffer = _commandBuffers.getBuffers()[imageIndex].getHandle();
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = waitSemaphores,
            .pWaitDstStageMask = waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = signalSemaphores,
        };

        vkResetFences(_device, 1, &currentFence);
        if(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, currentFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit draw command buffer!");
        }

        VkSwapchainKHR swapChains[] = {_swapChain};
        VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = signalSemaphores,
            .swapchainCount = 1,
            .pSwapchains = swapChains,
            .pImageIndices = &imageIndex,
            .pResults = nullptr // Optional
        };
        result = vkQueuePresentKHR(_presentQueue, &presentInfo);

        if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _framebufferResized) {
            _framebufferResized = false;
            recreateSwapChain();
        } else if(result != VK_SUCCESS) {
            throw std::runtime_error(fmt::format("Failed to present swap chain image. (Error: {})", result));
        }

        _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void recreateSwapChain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(_window, &width, &height);
        while(width == 0 || height == 0) {
            glfwGetFramebufferSize(_window, &width, &height);
            glfwWaitEvents();
        }

        vkDeviceWaitIdle(_device);

        cleanupSwapChain();

        createSwapChain();
        initSwapchain();
    }

    void cleanupSwapChain() {
        _swapChainFramebuffers.clear();

        // Only free up the command buffer, not the command pool
        const auto commandBufferHandles = _commandBuffers.getBuffersHandles();
        vkFreeCommandBuffers(_device, _commandPool, static_cast<uint32_t>(commandBufferHandles.size()), commandBufferHandles.data());
        _pipeline.destroy();
        _renderPass.destroy();
        _swapChainImageViews.clear();
        vkDestroySwapchainKHR(_device, _swapChain, nullptr);
    }

    void cleanup() {
        for(auto& f : _inFlightFences)
            f.destroy();
        for(auto& s : _renderFinishedSemaphore)
            s.destroy();
        for(auto& s : _imageAvailableSemaphore)
            s.destroy();

        cleanupSwapChain();
        _commandPool.destroy();
        _vertexBuffer.destroy();
        _vertexBufferMemory.free();

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