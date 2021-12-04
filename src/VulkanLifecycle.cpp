#include "Application.hpp"

#include <vulkan/Extension.hpp>

void Application::initVulkan() {
	if(!glfwVulkanSupported()) {
		error("GLFW: Vulkan Not Supported\n");
		return;
	}

	createInstance();
	loadExtensions(_instance);
	setupDebugMessenger();
	createSurface();
	auto physicalDevice = pickPhysicalDevice();
	if(!physicalDevice.isValid())
		throw std::runtime_error("Failed to find a suitable GPU!");
	_physicalDevice = physicalDevice;
	_device = Device{_surface, _physicalDevice, _requiredDeviceExtensions};
	loadExtensions(_device);
	auto queueIndices = _physicalDevice.getQueues(_surface);
	auto graphicsFamily = queueIndices.graphicsFamily.value();
	vkGetDeviceQueue(_device, graphicsFamily, 0, &_graphicsQueue);
	vkGetDeviceQueue(_device, queueIndices.presentFamily.value(), 0, &_presentQueue);

	createSwapChain();
	_commandPool.create(_device, graphicsFamily);
	_imguiCommandPool.create(_device, graphicsFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	_tempCommandPool.create(_device, graphicsFamily, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	for(auto& m : _scene.getMeshes()) {
		auto vertexDataSize = m.getVertexByteSize();

		// Prepare staging memory
		Buffer		 stagingBuffer;
		DeviceMemory stagingMemory;
		stagingBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexDataSize);
		auto stagingBufferMemReq = stagingBuffer.getMemoryRequirements();
		stagingMemory.allocate(_device,
							   _physicalDevice.findMemoryType(stagingBufferMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
							   stagingBufferMemReq.size);
		vkBindBufferMemory(_device, stagingBuffer, stagingMemory, 0);
		m.init(_device); // Pepare the final buffers
		m.upload(_device, stagingBuffer, stagingMemory, _tempCommandPool, _graphicsQueue);
		if(m.material) {
			m.material->uploadTextures(_device, graphicsFamily);
		}
	}

	// Load a blank image
	_blankTexture.source = "data/blank.png";
	auto	 blankPath = _blankTexture.source.string();
	STBImage image{blankPath};
	Images.try_emplace(blankPath);
	Images[blankPath].image.setDevice(_device);
	Images[blankPath].image.upload(image, graphicsFamily);
	Images[blankPath].imageView.create(_device, Images[blankPath].image, VK_FORMAT_R8G8B8A8_SRGB);
	_blankTexture.gpuImage = &Images[blankPath];
	_blankTexture.sampler =
		getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, Images[blankPath].image.getMipLevels());

	createAccelerationStructure();

	_pipelineCache.create(_device, PipelineCacheFilepath);

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

	initImGui(graphicsFamily);
}

void Application::createInstance() {
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
	/*
	uint32_t extensionCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	std::vector<VkExtensionProperties> extensions(extensionCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
	fmt::print("Available vulkan extensions ({}):\n", extensionCount);
	for(uint32_t i = 0; i < extensionCount; ++i)
		fmt::print("\t{}\n", extensions[i].extensionName);
	*/

	uint32_t	 glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> requestedExtensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

	if(_enableValidationLayers)
		requestedExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	VkInstanceCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = _enableValidationLayers ? &DebugMessengerCreateInfo : nullptr,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = _enableValidationLayers ? static_cast<uint32_t>(_validationLayers.size()) : 0,
		.ppEnabledLayerNames = _enableValidationLayers ? _validationLayers.data() : nullptr,
		.enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
		.ppEnabledExtensionNames = requestedExtensions.data(),
	};

	auto result = vkCreateInstance(&createInfo, nullptr, &_instance);
	if(result != VK_SUCCESS) {
		throw std::runtime_error(fmt::format("Failed to create Vulkan Instance (Error: {}).", result));
	}
	/*
	fmt::print("Created Vulkan Instance with {} extensions:\n", glfwExtensionCount);
	for(uint32_t i = 0; i < glfwExtensionCount; ++i)
		fmt::print("\t{}\n", glfwExtensions[i]);
	*/
}

void Application::cleanupVulkan() {
	vkDestroyAccelerationStructureKHR(_device, _topLevelAccelerationStructure, nullptr);
	for(const auto& blas : _bottomLevelAccelerationStructures)
		vkDestroyAccelerationStructureKHR(_device, blas, nullptr);
	for(auto& blasBuff : _blasBuffers)
		blasBuff.destroy();
	_blasBuffers.clear();
	for(auto& blasMem : _blasMemories)
		blasMem.free();
	_blasMemories.clear();
	_bottomLevelAccelerationStructures.clear();
	_tlasBuffer.destroy();
	_tlasMemory.free();
	_accStructInstancesBuffer.destroy();
	_accStructInstancesMemory.free();
	_accStructTransformBuffer.destroy();
	_accStructTransformMemory.free();
	for(size_t i = 0; i < _rayShaderBindingTablesCount; ++i) {
		_rayTracingShaderBindingTables[i].destroy();
		_rayTracingShaderBindingTablesMemory[i].free();
	}

	for(auto& f : _inFlightFences)
		f.destroy();
	for(auto& s : _renderFinishedSemaphore)
		s.destroy();
	for(auto& s : _imageAvailableSemaphore)
		s.destroy();

	cleanupSwapChain();

	_pipelineCache.save(PipelineCacheFilepath);
	_pipelineCache.destroy();

	cleanupUI();
	_commandPool.destroy();
	_tempCommandPool.destroy();
	for(auto& m : _scene.getMeshes()) {
		m.destroy();
	}
	Materials.clear();
	Images.clear();
	Samplers.clear();

	_device.destroy();
	if(_enableValidationLayers) {
		DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
	}
	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyInstance(_instance, nullptr);
}
