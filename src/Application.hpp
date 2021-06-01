#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <fmt/core.h>
#include <fmt/color.h>
#include <stdexcept>
#include <optional>
#include <set>

#include "Logger.hpp"
#include "vulkan/Device.hpp"

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	}
	else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr) {
		func(instance, debugMessenger, pAllocator);
	}
}

class HelloTriangleApplication {
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

	const std::vector<const char*> validationLayers = {
		"VK_LAYER_KHRONOS_validation"
	};

	const std::vector<const char*> requiredDeviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

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

	void initWindow() {
		fmt::print("Window initialisation... ");

		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Opt-out of creating an OpenGL Context
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		_window = glfwCreateWindow(Width, Height, "VulkanExp", nullptr, nullptr);
		if (_window == nullptr) {
			error("Error while creating GLFW Window. ");
		}
		success("Done.\n");
	}

	bool checkValidationLayerSupport() {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
		for (const char* layerName : validationLayers) {
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					layerFound = true;
					break;
				}
			}

			if (!layerFound) {
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
			.apiVersion = VK_MAKE_API_VERSION(1, 2, 0, 0),
		};

		if (_enableValidationLayers && !checkValidationLayerSupport()) {
			throw std::runtime_error("validation layers requested, but not available!");
		}

		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
		fmt::print("Available vulkan extensions ({}):\n", extensionCount);
		for (uint32_t i = 0; i < extensionCount; ++i)
			fmt::print("\t{}\n", extensions[i].extensionName);

		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector<const char*> requestedExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (_enableValidationLayers)
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
		if (result != VK_SUCCESS) {
			throw std::runtime_error(fmt::format("Failed to create Vulkan Instance (Error: {}).", result));
		}
		else {
			fmt::print("Created Vulkan Instance with {} extensions:\n", glfwExtensionCount);
			for (uint32_t i = 0; i < glfwExtensionCount; ++i)
				fmt::print("\t{}\n", glfwExtensions[i]);
		}
	}

	void setupDebugMessenger() {
		if (!_enableValidationLayers) return;
		if (CreateDebugUtilsMessengerEXT(_instance, &DebugMessengerCreateInfo, nullptr, &_debugMessenger) != VK_SUCCESS) {
			throw std::runtime_error("Failed to set up debug messenger!");
		}
	}

	bool checkDeviceExtensionSupport(VkPhysicalDevice device) const {
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> requiredExtensions(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end());

		for (const auto& extension : availableExtensions) {
			requiredExtensions.erase(extension.extensionName);
		}

		return requiredExtensions.empty();
	}

	unsigned int rateDevice(VkPhysicalDevice device) const {
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		// Required Capabilities

		PhysicalDevice::QueueFamilyIndices indices = PhysicalDevice(device).getQueues(_surface);
		if (!indices.graphicsFamily.has_value()) return 0;
		if (!checkDeviceExtensionSupport(device)) return 0;

		// Optional Capabilities
		int score = 1;
		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			score += 100;

		return score;
	}

	void createSurface() {
		if (glfwCreateWindowSurface(_instance, _window, nullptr, &_surface) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create window surface!");
		}
	}

	VkPhysicalDevice pickPhysicalDevice() const {
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
		if (deviceCount == 0) {
			throw std::runtime_error("Failed to find GPUs with Vulkan support!");
		}
		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		int maxScore = 0;
		for (const auto& device : devices) {
			auto score = rateDevice(device);
			if (score > maxScore) {
				physicalDevice = device;
				maxScore = score;
				break;
			}
		}
		return physicalDevice;
	}


	void initVulkan() {
		fmt::print("Vulkan initialisation... ");

		createInstance();
		setupDebugMessenger();
		createSurface();
		auto physicalDeviceHandle = pickPhysicalDevice();
		if (physicalDeviceHandle == VK_NULL_HANDLE)
			throw std::runtime_error("Failed to find a suitable GPU!");
		_physicalDevice = PhysicalDevice(physicalDeviceHandle);
		_device = Device(_surface, _physicalDevice, requiredDeviceExtensions);
		auto queueIndices = _physicalDevice.getQueues(_surface);
		vkGetDeviceQueue(_device, queueIndices.graphicsFamily.value(), 0, &_graphicsQueue);
		vkGetDeviceQueue(_device, queueIndices.presentFamily.value(), 0, &_presentQueue);

		success("Done.\n");
	}

	void mainLoop() {
		while (!glfwWindowShouldClose(_window)) {
			glfwPollEvents();
		}
	}

	void cleanup() {
		vkDestroyDevice(_device, nullptr);
		if (_enableValidationLayers) {
			DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
		}
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyInstance(_instance, nullptr);
		glfwDestroyWindow(_window);
		glfwTerminate();
	}

	static const VkDebugUtilsMessageSeverityFlagBitsEXT ValidationLayerDebugLevel = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData) {

		if (messageSeverity >= ValidationLayerDebugLevel) {
			switch (messageSeverity) {
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

	const VkDebugUtilsMessengerCreateInfoEXT DebugMessengerCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debugCallback,
		.pUserData = nullptr
	};
};