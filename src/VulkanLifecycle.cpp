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
	_physicalDevice = pickPhysicalDevice();
	if(!_physicalDevice.isValid())
		throw std::runtime_error("Failed to find a suitable GPU!");

	PhysicalDevice::QueueFamilyIndex graphicsFamily = _physicalDevice.getGraphicsQueueFamilyIndex();
	PhysicalDevice::QueueFamilyIndex computeFamily = _physicalDevice.getComputeQueueFamilyIndex();
	PhysicalDevice::QueueFamilyIndex transfertFamily = _physicalDevice.getTransfertQueueFamilyIndex();
	PhysicalDevice::QueueFamilyIndex presentFamily = _physicalDevice.getPresentQueueFamilyIndex(_surface);

	float								 queuePriorities[4]{1.0f};
	std::vector<VkDeviceQueueCreateInfo> queues;
	uint32_t							 queuesFromGraphicsFamily = 1;
	if(computeFamily != graphicsFamily)
		++queuesFromGraphicsFamily;
	if(transfertFamily != graphicsFamily)
		++queuesFromGraphicsFamily;
	queues.push_back({
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = graphicsFamily,
		.queueCount = queuesFromGraphicsFamily,
		.pQueuePriorities = queuePriorities,
	});
	if(computeFamily != graphicsFamily)
		queues.push_back({
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = computeFamily,
			.queueCount = 1,
			.pQueuePriorities = queuePriorities,
		});
	if(transfertFamily != graphicsFamily)
		queues.push_back({
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = transfertFamily,
			.queueCount = 1,
			.pQueuePriorities = queuePriorities,
		});
	if(presentFamily != graphicsFamily)
		queues.push_back({
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = presentFamily,
			.queueCount = 1,
			.pQueuePriorities = queuePriorities,
		});

	_device = Device{_surface, _physicalDevice, queues, _requiredDeviceExtensions};
	loadExtensions(_device);
	vkGetDeviceQueue(_device, graphicsFamily, 0, &_graphicsQueue);
	// If we don't have a dedicaded transfert, try to still use a one disctint from our graphics queue
	if(transfertFamily == graphicsFamily) {
		if(_physicalDevice.getQueueFamilies()[graphicsFamily].queueCount > 1)
			vkGetDeviceQueue(_device, transfertFamily, 1, &_transfertQueue);
		else
			vkGetDeviceQueue(_device, transfertFamily, 0, &_transfertQueue);
	} else
		vkGetDeviceQueue(_device, transfertFamily, 0, &_transfertQueue);
	if(computeFamily == graphicsFamily) {
		if(_physicalDevice.getQueueFamilies()[graphicsFamily].queueCount > 1)
			vkGetDeviceQueue(_device, computeFamily, 1, &_computeQueue);
		else
			vkGetDeviceQueue(_device, computeFamily, 0, &_computeQueue);
	} else
		vkGetDeviceQueue(_device, computeFamily, 0, &_computeQueue);
	vkGetDeviceQueue(_device, presentFamily, 0, &_presentQueue);

	createSwapChain();
	_commandPool.create(_device, graphicsFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	_imguiCommandPool.create(_device, graphicsFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	_transfertCommandPool.create(_device, transfertFamily, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	_computeCommandPool.create(_device, computeFamily, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	// Prepare a staging buffer for all the following copies.
	// Compute the maximum memory we'll need.
	size_t stagingBufferSize = 0;
	// Meshes
	{
		size_t buffSize = 0;
		for(const auto& m : _scene.getMeshes())
			for(const auto& sm : m.SubMeshes) {
				if(sm.getVertexByteSize() > buffSize)
					buffSize = sm.getVertexByteSize();
			}
		stagingBufferSize = std::max(stagingBufferSize, buffSize);
	}
	{
		size_t buffSize = 0;
		for(const auto& m : _scene.getMeshes())
			for(const auto& sm : m.SubMeshes) {
				if(sm.getVertexByteSize() > buffSize)
					buffSize = sm.getVertexByteSize();
			}
		stagingBufferSize = std::max(stagingBufferSize, buffSize);
	}
	// Materials
	std::vector<Material::GPUData> materialGpu;
	for(const auto& material : Materials)
		materialGpu.push_back(material.getGPUData());
	stagingBufferSize = std::max(stagingBufferSize, materialGpu.size() * sizeof(Material::GPUData));
	// Textures, they're not loaded yet, we could, but we'll just take an upper bound for now
	stagingBufferSize = std::max(stagingBufferSize, static_cast<size_t>(4 * 8 * 4096 * 4096)); // FIXME

	Buffer		 stagingBuffer;
	DeviceMemory stagingMemory;
	stagingBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, stagingBufferSize);
	stagingMemory.allocate(_device, stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	{
		QuickTimer qt("Mesh Generation");
		for(auto& m : _scene.getMeshes())
			for(auto& sm : m.SubMeshes)
				sm.init(_device);		// Pepare the final buffers
		_scene.allocateMeshes(_device); // Allocate memory for all meshes and bind the buffers
		for(auto& m : _scene.getMeshes())
			for(auto& sm : m.SubMeshes)
				sm.upload(_device, stagingBuffer, stagingMemory, _transfertCommandPool, _transfertQueue);
		uploadTextures(_device, _graphicsQueue, _commandPool, stagingBuffer);
	}

	MaterialBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						  materialGpu.size() * sizeof(Material::GPUData));
	MaterialMemory.allocate(_device, MaterialBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	stagingMemory.fill(materialGpu.data(), materialGpu.size());
	MaterialBuffer.copyFromStagingBuffer(_transfertCommandPool, stagingBuffer, materialGpu.size() * sizeof(Material::GPUData), _transfertQueue);

	{
		for(auto& m : _probeMesh.getMeshes())
			for(auto& sm : m.SubMeshes)
				sm.init(_device); // Prepare the final buffers
		_probeMesh.allocateMeshes(_device);
		for(auto& m : _probeMesh.getMeshes())
			for(auto& sm : m.SubMeshes)
				sm.upload(_device, stagingBuffer, stagingMemory, _transfertCommandPool, _transfertQueue);
	}

	// Load a blank image
	_engineTextures.reserve(1024);
	_blankTexture = &_engineTextures.emplace_back();
	_blankTexture->source = "data/blank.png";
	_blankTexture->format = VK_FORMAT_R8G8B8A8_SRGB;
	_blankTexture->sampler =
		getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0);

	for(size_t i = 0; i < 64; ++i) {
		_blueNoiseTextures[i] = &_engineTextures.emplace_back();
		_blueNoiseTextures[i]->source = fmt::format("data/BlueNoise/64_64/LDR_RGBA_{}.png", i);
		_blueNoiseTextures[i]->format = VK_FORMAT_R8G8B8A8_UNORM;
		_blueNoiseTextures[i]->sampler =
			getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0);
	}

	for(auto& tex : _engineTextures) {
		auto	 path = tex.source.string();
		STBImage source{path};
		Images.try_emplace(path);
		auto& img = Images[path];
		tex.gpuImage = &img;
		img.image.setDevice(_device);
		img.image.upload(source, graphicsFamily, tex.format);
		img.imageView.create(_device, img.image, tex.format);
	}

	auto bounds = _scene.computeBounds();
	_irradianceProbes.init(_device, transfertFamily, computeFamily, bounds.min, bounds.max);

	_scene.createAccelerationStructure(_device);

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
	uiOnSwapChainReady();
}

void Application::uploadMaterials() {
	std::vector<Material::GPUData> materialGpu;
	for(const auto& material : Materials)
		materialGpu.push_back(material.getGPUData());
	Buffer		 stagingBuffer;
	DeviceMemory stagingMemory;
	stagingBuffer.create(_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, materialGpu.size() * sizeof(Material::GPUData));
	stagingMemory.allocate(_device, stagingBuffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	stagingMemory.fill(materialGpu.data(), materialGpu.size());
	MaterialBuffer.copyFromStagingBuffer(_transfertCommandPool, stagingBuffer, materialGpu.size() * sizeof(Material::GPUData), _transfertQueue);
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
	// We souldn't have to recreate the underlying buffer/memory on swapchain re-creation.
	_directLightShaderBindingTable.destroy();
	_reflectionShaderBindingTable.destroy();
	_raytracingShaderBindingTable.destroy();

	for(auto& f : _inFlightFences)
		f.destroy();
	for(auto& s : _renderFinishedSemaphore)
		s.destroy();
	for(auto& s : _imageAvailableSemaphore)
		s.destroy();

	cleanupSwapChain();

	_irradianceProbes.destroy();
	_probeMesh.free(_device);

	_pipelineCache.save(PipelineCacheFilepath);
	_pipelineCache.destroy();

	cleanupUI();
	_commandPool.destroy();
	_transfertCommandPool.destroy();
	_computeCommandPool.destroy();
	_scene.free(_device);
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
