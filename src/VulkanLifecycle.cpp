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
	{
		QuickTimer qt("Mesh Generation");
		size_t	   buffSize = 0;
		for(const auto& m : _scene.getMeshes())
			for(const auto& sm : m.SubMeshes) {
				if(sm.getVertexByteSize() > buffSize)
					buffSize = sm.getVertexByteSize();
			}
		// Prepare staging memory
		Buffer		 stagingBuffer;
		DeviceMemory stagingMemory;
		stagingBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, buffSize);
		auto stagingBufferMemReq = stagingBuffer.getMemoryRequirements();
		stagingMemory.allocate(_device, stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		for(auto& m : _scene.getMeshes()) {
			for(auto& sm : m.SubMeshes)
				sm.init(_device); // Pepare the final buffers
		}
		_scene.allocateMeshes(_device); // Allocate memory for all meshes and bind the buffers
		for(auto& m : _scene.getMeshes()) {
			for(auto& sm : m.SubMeshes)
				sm.upload(_device, stagingBuffer, stagingMemory, _tempCommandPool, _graphicsQueue);
		}
		uploadTextures(_device, graphicsFamily);
	}
	std::vector<Material::GPUData> materialGpu;
	for(const auto& material : Materials) {
		materialGpu.push_back(material.getGPUData());
	}
	Buffer		 stagingBuffer;
	DeviceMemory stagingMemory;
	stagingBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, materialGpu.size() * sizeof(Material::GPUData));
	auto stagingBufferMemReq = stagingBuffer.getMemoryRequirements();
	stagingMemory.allocate(_device, stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	MaterialBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						  materialGpu.size() * sizeof(Material::GPUData));
	MaterialMemory.allocate(_device, MaterialBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	stagingMemory.fill(materialGpu.data(), materialGpu.size());
	MaterialBuffer.copyFromStagingBuffer(_tempCommandPool, stagingBuffer, materialGpu.size() * sizeof(Material::GPUData), _graphicsQueue);

	{
		size_t buffSize = 0;
		for(const auto& m : _scene.getMeshes())
			for(const auto& sm : m.SubMeshes) {
				if(sm.getVertexByteSize() > buffSize)
					buffSize = sm.getVertexByteSize();
			}
		Buffer		 stagingBuffer;
		DeviceMemory stagingMemory;
		stagingBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 2 * buffSize);
		auto stagingBufferMemReq = stagingBuffer.getMemoryRequirements();
		stagingMemory.allocate(_device, stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		for(auto& m : _probeMesh.getMeshes()) {
			for(auto& sm : m.SubMeshes)
				sm.init(_device); // Pepare the final buffers
		}
		_probeMesh.allocateMeshes(_device);
		for(auto& m : _probeMesh.getMeshes()) {
			for(auto& sm : m.SubMeshes)
				sm.upload(_device, stagingBuffer, stagingMemory, _tempCommandPool, _graphicsQueue);
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

	auto bounds = _scene.computeBounds();
	_irradianceProbes.init(_device, graphicsFamily, bounds.min, bounds.max);

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

	if(_enableValidationLayers && !checkValidationLayerSupport())
		throw std::runtime_error("Validation layers requested, but not available!");

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

	VK_CHECK(vkCreateInstance(&createInfo, nullptr, &_instance));
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

	_irradianceProbes.destroy();
	_probeMesh.free();

	_pipelineCache.save(PipelineCacheFilepath);
	_pipelineCache.destroy();

	cleanupUI();
	_commandPool.destroy();
	_tempCommandPool.destroy();
	_scene.free();
	_probeMesh.free();
	MaterialBuffer.destroy();
	MaterialMemory.free();
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
