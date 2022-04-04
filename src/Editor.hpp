#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/color.h>
#include <fmt/core.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <FileWatch.hpp>

#include "Logger.hpp"
#include "Scene.hpp"
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
#include <Camera.hpp>
#include <vulkan/Image.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <imgui.h>

#include <ImGuizmo/ImGuizmo.h>

#include <EditHistory.hpp>
#include <IrradianceProbes.hpp>
#include <KeyboardShortcut.hpp>
#include <Light.hpp>
#include <PipelineCache.hpp>
#include <QuickTimer.hpp>
#include <Renderer.hpp>
#include <RollingBuffer.hpp>

struct CameraBuffer {
	glm::mat4 view;
	glm::mat4 proj;
	glm::vec3 origin;
	uint32_t  frameIndex;
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

class Editor {
  public:
	void run();

  private:
	const uint32_t InitialWidth = 1920;
	const uint32_t InitialHeight = 1080;

	const std::vector<const char*> _validationLayers = {"VK_LAYER_KHRONOS_validation", "VK_LAYER_LUNARG_monitor"};

	const std::vector<const char*> _requiredDeviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
		VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,
	};

#ifdef NDEBUG
	const bool _enableValidationLayers = false;
#else
	const bool _enableValidationLayers = true;
#endif

	bool _dirtyShaders = false; // Re-compile on startup?
	// Auto re-compile shaders
	filewatch::FileWatch<std::string> _shadersFileWatcher{"./src/shaders/", [&](const std::string& file, const filewatch::Event event_type) { _dirtyShaders = true; }};

	GLFWwindow*				 _window = nullptr;
	uint32_t				 _width = InitialWidth;
	uint32_t				 _height = InitialHeight;
	VkInstance				 _instance;
	VkDebugUtilsMessengerEXT _debugMessenger;
	PhysicalDevice			 _physicalDevice;
	Device					 _device;
	VkSurfaceKHR			 _surface;
	VkQueue					 _graphicsQueue;
	VkQueue					 _computeQueue;
	VkQueue					 _transfertQueue;
	VkQueue					 _presentQueue;
	CommandPool				 _computeCommandPool;
	RollingBuffer<float>	 _frameTimes;
	RollingBuffer<float>	 _gbufferTimes;
	RollingBuffer<float>	 _reflectionDirectLightTimes;
	RollingBuffer<float>	 _reflectionDirectLightFilterTimes;
	RollingBuffer<float>	 _gatherTimes;
	RollingBuffer<float>	 _presentTimes;

	EditHistory _history;

	bool		_drawUI = true;
	float		_timeScale = 1.0f;
	LightBuffer _light;
	bool		_deriveLightPositionFromTime = false;
	float		_dayCycleSpeed = 48.0;
	int			_dayOfTheYear = 160;
	int			_hour = 12;
	float		_minute = 0;
	float		_longitude = 5;
	float		_latitude = 45;
	int			_utctimezone = 1; // _timezone is a macro...

	Buffer		 _stagingBuffer;
	DeviceMemory _stagingMemory;

	std::vector<Texture> _engineTextures;
	Texture*			 _blankTexture = nullptr;
	Texture*			 _blueNoiseTextures[64]{nullptr};

	VkSwapchainKHR		   _swapChain;
	VkFormat			   _swapChainImageFormat;
	VkExtent2D			   _swapChainExtent;
	std::vector<VkImage>   _swapChainImages;
	std::vector<ImageView> _swapChainImageViews;
	VkFormat			   _depthFormat;
	uint32_t			   _lastImageIndex = 0;

	CommandPool						 _transfertCommandPool;
	std::vector<DescriptorSetLayout> _descriptorSetLayouts;
	DescriptorPool					 _descriptorPool;

	inline static constexpr char const* PipelineCacheFilepath = "./vulkan_pipeline.cache";
	PipelineCache						_pipelineCache;

	bool							 _dirtyHierarchy = false;		  // Node Hierachy has changed shape
	bool							 _outdatedCommandBuffers = false; // Re-record command buffers at the start of the next frame
	Image							 _depthImage;
	ImageView						 _depthImageView;
	const uint32_t					 _gbufferSize = 5; // How many images the gbuffer pass generates
	std::vector<Image>				 _gbufferImages;
	std::vector<ImageView>			 _gbufferImageViews;
	std::vector<Framebuffer>		 _gbufferFramebuffers;
	RenderPass						 _gbufferRenderPass;
	DescriptorPool					 _gbufferDescriptorPool;
	std::vector<DescriptorSetLayout> _gbufferDescriptorSetLayouts;
	Pipeline						 _gbufferPipeline;
	Pipeline						 _gbufferSkinnedPipeline;
	std::vector<Image>				 _directLightImages;
	std::vector<ImageView>			 _directLightImageViews;
	std::vector<Image>				 _directLightIntermediateFilterImages;
	std::vector<ImageView>			 _directLightIntermediateFilterImageViews;
	std::vector<Image>				 _reflectionImages;
	std::vector<ImageView>			 _reflectionImageViews;
	std::vector<Image>				 _reflectionIntermediateFilterImages;
	std::vector<ImageView>			 _reflectionIntermediateFilterImageViews;
	DescriptorPool					 _directLightDescriptorPool;
	DescriptorSetLayout				 _directLightDescriptorSetLayout;
	Pipeline						 _directLightPipeline;
	ShaderBindingTable				 _directLightShaderBindingTable;
	DescriptorPool					 _directLightFilterDescriptorPool;
	DescriptorSetLayout				 _directLightFilterDescriptorSetLayout;
	Pipeline						 _directLightFilterPipelineX;
	Pipeline						 _directLightFilterPipelineY;
	DescriptorPool					 _reflectionDescriptorPool;
	DescriptorSetLayout				 _reflectionDescriptorSetLayout;
	Pipeline						 _reflectionPipeline;
	ShaderBindingTable				 _reflectionShaderBindingTable;
	DescriptorPool					 _reflectionFilterDescriptorPool;
	DescriptorSetLayout				 _reflectionFilterDescriptorSetLayout;
	Pipeline						 _reflectionFilterPipelineX;
	Pipeline						 _reflectionFilterPipelineY;
	std::vector<Framebuffer>		 _gatherFramebuffers;
	RenderPass						 _gatherRenderPass;
	DescriptorPool					 _gatherDescriptorPool;
	DescriptorSetLayout				 _gatherDescriptorSetLayout;
	Pipeline						 _gatherPipeline;
	CommandPool						 _commandPool;
	CommandBuffers					 _commandBuffers;
	CommandBuffers					 _copyCommandBuffers;
	std::vector<Semaphore>			 _renderFinishedSemaphore;
	std::vector<Semaphore>			 _imageAvailableSemaphore;
	std::vector<Fence>				 _inFlightFences;
	std::vector<VkFence>			 _imagesInFlight;
	std::vector<QueryPool>			 _mainTimingQueryPools;

	bool _enableReflections = true;

	void createGBufferPass();
	void createGBufferRenderPass();
	void createGBufferFramebuffers();
	void createGBufferPipeline();
	void writeGBufferDescriptorSets();
	void destroyGBufferPipeline();

	void createReflectionPass();
	void destroyReflectionPipeline();
	void writeReflectionDescriptorSets();

	void createDirectLightPass();
	void destroyDirectLightPipeline();
	void writeDirectLightDescriptorSets();

	void createGatherPass();
	void createGatherRenderPass();
	void createGatherFramebuffers();
	void createGatherPipeline();
	void destroyGatherPass();
	void destroyGatherPipeline();

	void createProbeDebugPass();

	size_t				_uboStride = 0;
	std::vector<Buffer> _cameraUniformBuffers;
	DeviceMemory		_cameraUniformBuffersMemory;
	size_t				_lightUboStride = 0;
	std::vector<Buffer> _lightUniformBuffers;
	DeviceMemory		_lightUniformBuffersMemory;

	VkDescriptorPool		 _imguiDescriptorPool;
	std::vector<Framebuffer> _presentFramebuffers;
	RenderPass				 _imguiRenderPass;
	CommandPool				 _imguiCommandPool;
	CommandBuffers			 _imguiCommandBuffers;

	Scene	 _scene;
	Renderer _renderer;
	Renderer _editorRenderer;

	bool							 _probeDebug = false;
	bool							 _irradianceProbeAutoUpdate = true;
	IrradianceProbes				 _irradianceProbes;
	Scene							 _probeMesh;
	RenderPass						 _probeDebugRenderPass;
	DescriptorPool					 _probeDebugDescriptorPool;
	std::vector<DescriptorSetLayout> _probeDebugDescriptorSetLayouts;
	Pipeline						 _probeDebugPipeline;
	std::vector<Framebuffer>		 _probeDebugFramebuffers;

	// Raytracing test
	bool				   _raytracingDebug = false;
	std::vector<Image>	   _rayTraceStorageImages;
	std::vector<ImageView> _rayTraceStorageImageViews;
	CommandBuffers		   _rayTraceCommandBuffers;
	DescriptorSetLayout	   _rayTracingDescriptorSetLayout;
	DescriptorPool		   _rayTracingDescriptorPool;
	PipelineLayout		   _rayTracingPipelineLayout;
	Pipeline			   _rayTracingPipeline;
	ShaderBindingTable	   _raytracingShaderBindingTable;
	void				   createStorageImage();
	void				   createRaytracingDescriptorSets();
	void				   writeRaytracingDescriptorSets();
	void				   createRayTracingPipeline();
	void				   destroyRayTracingPipeline();
	void				   recordRayTracingCommands();

	bool _framebufferResized = false;

	// See https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPresentModeKHR.html
	// TLDR (as I understand it:) ):
	// VK_PRESENT_MODE_FIFO_KHR to limit the frame rate to the display refresh rate (basically VSync, increases input lag)
	// VK_PRESENT_MODE_MAILBOX_KHR to allow submiting gpu work more frequently, while presenting only on display refresh
	VkPresentModeKHR _preferedPresentMode = VK_PRESENT_MODE_FIFO_KHR; // VK_PRESENT_MODE_MAILBOX_KHR

	const int MAX_FRAMES_IN_FLIGHT = 2;
	size_t	  _currentFrame = 0;
	uint32_t  _frameIndex = 0;

	std::unordered_map<KeyboardShortcut, std::function<void()>> _shortcuts;
	bool														_controlCamera = false;
	Camera														_camera{glm::vec3(-14.0f, 15.0f, 18.0f), glm::normalize(glm::vec3(1.0, -1.0f, -1.0f))};
	double														_mouse_x = 0, _mouse_y = 0;

	entt::entity		_selectedNode = entt::null;
	bool				_useSnap = false;
	glm::vec3			_snapOffset{1.0};
	float				_snapAngleOffset{90.0};
	float				_snapScaleOffset{0.5};
	ImGuizmo::OPERATION _currentGizmoOperation = ImGuizmo::OPERATION::TRANSLATE;
	ImGuizmo::MODE		_currentGizmoMode = ImGuizmo::MODE::WORLD;

	void createInstance();
	void createSwapChain();
	void initSwapChain();
	void initUniformBuffers();
	void recordCommandBuffers();
	void recreateSwapChain();
	void cleanupSwapChain();
	void createImGuiRenderPass();
	void uiOnTextureChange();
	void recordUICommandBuffer(size_t index);
	void uploadScene();
	void uploadMaterials();

	void trySelectNode();
	void duplicateSelectedNode();
	void deleteSelectedNode();

	void compileShaders() {
		// Could use "start" to launch it asynchronously, but I'm not sure if there's a way to react to the command finishing
		// Could use popen() instead of system() to capture the output too.
		{
			QuickTimer qt("Shader Compilation");
			print("\n");
			system("powershell.exe -ExecutionPolicy RemoteSigned .\\compile_shaders.ps1");
		}
		// Fixme: We can probably do a lot less :) (Like only recreating the concerned pipelines)
		vkDeviceWaitIdle(_device);
		{
			QuickTimer qt("Pipelines re-creation");
			destroyGBufferPipeline();
			destroyDirectLightPipeline();
			destroyReflectionPipeline();
			destroyGatherPipeline();
			destroyRayTracingPipeline();
			_irradianceProbes.destroyPipeline();
			_renderer.destroyVertexSkinningPipeline();
			createGBufferPipeline();
			createDirectLightPass();
			createReflectionPass();
			createGatherPipeline();
			createRayTracingPipeline();
			createRaytracingDescriptorSets();
			recordRayTracingCommands();
			_irradianceProbes.createPipeline(_pipelineCache);
			_renderer.createVertexSkinningPipeline(_pipelineCache);
			onTLASCreation();
			_outdatedCommandBuffers = true;
		}
	}

	void setupDebugMessenger() {
		if(!_enableValidationLayers)
			return;
		VK_CHECK(CreateDebugUtilsMessengerEXT(_instance, &DebugMessengerCreateInfo, nullptr, &_debugMessenger));
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

	bool checkDeviceExtensionSupport(const PhysicalDevice& device) const {
		const auto&			  availableExtensions = device.getExtensions();
		std::set<std::string> requiredExtensions(_requiredDeviceExtensions.begin(), _requiredDeviceExtensions.end());
		for(const auto& extension : availableExtensions)
			requiredExtensions.erase(extension.extensionName);
		return requiredExtensions.empty();
	}

	unsigned int rateDevice(const PhysicalDevice& device) const {
		VkPhysicalDeviceProperties deviceProperties = device.getProperties();

		// Required Capabilities
		PhysicalDevice::QueueFamilyIndices indices = device.getQueues(_surface);
		if(!indices.graphicsFamily.has_value())
			return 0;
		if(!checkDeviceExtensionSupport(device))
			return 0;
		// FIXME: This should be made optional
		if(!device.getFeatures().samplerAnisotropy)
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
		VK_CHECK(glfwCreateWindowSurface(_instance, _window, nullptr, &_surface));
	}

	PhysicalDevice pickPhysicalDevice() const {
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
		if(deviceCount == 0)
			throw std::runtime_error("Failed to find GPUs with Vulkan support!");
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

	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
	VkPresentModeKHR   chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
	VkExtent2D		   chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

	void initWindow();
	void initVulkan();
	void initImGui(uint32_t queueFamily);

	void mainLoop();

	void drawFrame();
	void drawUI();

	void onTLASCreation();

	void cameraControl(float dt);
	void updateUniformBuffer(uint32_t currentImage);

	void cleanupUI();
	void cleanupVulkan();

	void cleanup() {
		cleanupVulkan();

		glfwDestroyWindow(_window);
		glfwTerminate();
	}

	static const VkDebugUtilsMessageSeverityFlagBitsEXT ValidationLayerDebugLevel = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	// VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

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

	const VkDebugUtilsMessengerCreateInfoEXT DebugMessengerCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debugCallback,
		.pUserData = nullptr,
	};

	static void sFramebufferResizeCallback(GLFWwindow* window, int width, int height);
	static void sScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
	static void sMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	static void sDropCallback(GLFWwindow* window, int pathCount, const char* paths[]);
	static void sKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};
