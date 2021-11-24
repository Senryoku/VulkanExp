#pragma once

#include <chrono>
#include <functional>
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
#include "glTF.hpp"
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
		const auto& mesure = [&](const std::string& name, const std::function<void()>& call) {
			auto t1 = std::chrono::high_resolution_clock::now();
			call();
			auto t2 = std::chrono::high_resolution_clock::now();
			auto d = t2 - t1;
			if(d.count() > 10000000)
				std::cout << name << ": " << std::chrono::duration_cast<std::chrono::milliseconds>(d) << '\n';
			else if(d.count() > 10000)
				std::cout << name << ": " << std::chrono::duration_cast<std::chrono::microseconds>(d) << '\n';
			else
				std::cout << name << ": " << std::chrono::duration_cast<std::chrono::nanoseconds>(d) << '\n';
		};
		mesure("glTF load", [&]() { _model.load("./data/models/Sponza/glTF/Sponza.gltf"); });
		/*
		mesure("_mesh.loadOBJ", [&]() { _mesh.loadOBJ("data/models/lucy.obj"); });
		mesure("_mesh.normalizeVertices", [&]() { _mesh.normalizeVertices(); });
		mesure("_mesh.computeVertexNormals", [&]() { _mesh.computeVertexNormals(); });
		*/
		mesure("initWindow", [&]() { initWindow(); });
		mesure("initVulkan", [&]() { initVulkan(); });
		mainLoop();
		cleanup();
	}

  private:
	const uint32_t InitialWidth = 1280;
	const uint32_t InitialHeight = 800;

	const std::vector<const char*> _validationLayers = {"VK_LAYER_KHRONOS_validation"};

	const std::vector<const char*> _requiredDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
	const bool _enableValidationLayers = false;
#else
	const bool _enableValidationLayers = true;
#endif

	bool _dirtyShaders = true; // Re-compile on startup
	// Auto re-compile shaders
	filewatch::FileWatch<std::string> _shadersFileWatcher{"./src/shaders/", [&](const std::string& file, const filewatch::Event event_type) { _dirtyShaders = true; }};

	GLFWwindow*				 _window = nullptr;
	VkInstance				 _instance;
	VkDebugUtilsMessengerEXT _debugMessenger;
	PhysicalDevice			 _physicalDevice;
	Device					 _device;
	VkSurfaceKHR			 _surface;
	VkQueue					 _graphicsQueue;
	VkQueue					 _presentQueue;

	VkSwapchainKHR		   _swapChain;
	std::vector<VkImage>   _swapChainImages;
	VkFormat			   _swapChainImageFormat;
	VkExtent2D			   _swapChainExtent;
	std::vector<ImageView> _swapChainImageViews;

	VkFormat  _depthFormat;
	Image	  _depthImage;
	ImageView _depthImageView;

	RenderPass				 _renderPass;
	DescriptorSetLayout		 _descriptorSetLayout;
	Pipeline				 _pipeline;
	std::vector<Framebuffer> _swapChainFramebuffers;
	CommandPool				 _commandPool;
	CommandPool				 _tempCommandPool;
	CommandBuffers			 _commandBuffers;
	std::vector<Semaphore>	 _renderFinishedSemaphore;
	std::vector<Semaphore>	 _imageAvailableSemaphore;
	std::vector<Fence>		 _inFlightFences;
	std::vector<VkFence>	 _imagesInFlight;

	std::vector<Buffer> _uniformBuffers;
	DeviceMemory		_uniformBuffersMemory;

	DescriptorPool _descriptorPool;

	VkDescriptorPool		 _imguiDescriptorPool;
	std::vector<Framebuffer> _imguiFramebuffers;
	RenderPass				 _imguiRenderPass;
	CommandPool				 _imguiCommandPool;
	CommandBuffers			 _imguiCommandBuffers;

	Mesh		 _mesh;
	glTF		 _model;
	DeviceMemory _deviceMemory;

	bool _framebufferResized = false;

	// See https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPresentModeKHR.html
	// TLDR (as I understand it:) ):
	// VK_PRESENT_MODE_FIFO_KHR to limit the frame rate to the display refresh rate (basically VSync, increases input lag)
	// VK_PRESENT_MODE_MAILBOX_KHR to allow submiting gpu work more frequently, while presenting only on display refresh
	VkPresentModeKHR _preferedPresentMode = VK_PRESENT_MODE_FIFO_KHR; // VK_PRESENT_MODE_MAILBOX_KHR

	const int MAX_FRAMES_IN_FLIGHT = 2;
	size_t	  _currentFrame = 0;

	float	  _cameraZoom = 10.0;
	glm::vec3 _cameraTarget{0.0f, 0.0f, 0.0f};
	float	  _farPlane = 4000.0f;
	float	  _nearPlane = 1.0f;

	bool   _moving = false;
	double _last_xpos = 0, _last_ypos = 0;

	void createInstance();
	void createSwapChain();
	void initSwapChain();
	void recordCommandBuffers();
	void recreateSwapChain();
	void cleanupSwapChain();

	void compileShaders() {
		// Could use "start" to launch it asynchronously, but I'm not sure if there's a way to react to the command finishing
		// Could use popen() instead of system() to capture the output too.
		system("powershell.exe -ExecutionPolicy RemoteSigned .\\compile_shaders.ps1");
		// Fixme: We can probably do a lot less :) (Like only recreating the pipeline, which could even be done in another thread)
		recreateSwapChain();
	}

	static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
		if(ImGui::GetIO().WantCaptureMouse)
			return;
		auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
		if(yoffset > 0)
			app->_cameraZoom *= 1.f / 1.2f;
		else
			app->_cameraZoom *= 1.2f;
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
		for(const char* layerName : _validationLayers) {
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

		std::set<std::string> requiredExtensions(_requiredDeviceExtensions.begin(), _requiredDeviceExtensions.end());

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
		// FIXME: This should be made optional
		if(!deviceFeatures.samplerAnisotropy)
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
		unsigned int   maxScore = 0;
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
			// if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
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

	void initWindow();
	void initVulkan();
	void initImGui(uint32_t queueFamily);

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
	void drawUI();

	void updateUniformBuffer(uint32_t currentImage);

	void cleanupUI();
	void cleanupVulkan();

	void cleanup() {
		cleanupVulkan();

		glfwDestroyWindow(_window);
		glfwTerminate();
	}

	static const VkDebugUtilsMessageSeverityFlagBitsEXT ValidationLayerDebugLevel = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
														const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {

		if(messageSeverity >= ValidationLayerDebugLevel) {
			switch(messageSeverity) {
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: fmt::print("Validation layer: {}\n", pCallbackData->pMessage); break;
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: warn("Validation layer: {}\n", pCallbackData->pMessage); break;
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: error("Validation layer: {}\n", pCallbackData->pMessage); break;
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
