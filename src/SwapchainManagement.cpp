#include <Editor.hpp>

VkSurfaceFormatKHR Editor::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
	for(const auto& availableFormat : availableFormats) {
		if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return availableFormat;
		}
	}

	return availableFormats[0];
}

VkPresentModeKHR Editor::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
	for(const auto& availablePresentMode : availablePresentModes) {
		if(availablePresentMode == _preferedPresentMode) {
			return availablePresentMode;
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Editor::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
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

void Editor::createSwapChain() {
	auto swapChainSupport = _physicalDevice.getSwapChainSupport(_surface);

	VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR   presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D		   extent = chooseSwapExtent(swapChainSupport.capabilities);

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
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.preTransform = swapChainSupport.capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, // VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR ?
		.presentMode = presentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE,
	};

	auto	 indices = _physicalDevice.getQueues(_surface);
	uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

	if(indices.graphicsFamily != indices.presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	} else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;	  // Optional
		createInfo.pQueueFamilyIndices = nullptr; // Optional
	}

	VK_CHECK(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapChain));

	vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, nullptr);
	_swapChainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, _swapChainImages.data());

	_swapChainImageFormat = surfaceFormat.format;
	_swapChainExtent = extent;

	for(size_t i = 0; i < _swapChainImages.size(); i++)
		_swapChainImageViews.push_back(ImageView{_device, _swapChainImages[i], _swapChainImageFormat});

	// Create GBuffer Images & Views
	_gbufferImages.resize(_gbufferSize * _swapChainImages.size());
	_gbufferImageViews.resize(_gbufferSize * _swapChainImages.size());
	_directLightImages.resize(2 * _swapChainImages.size());
	_directLightImageViews.resize(2 * _swapChainImages.size());
	_directLightIntermediateFilterImages.resize(_swapChainImages.size());
	_directLightIntermediateFilterImageViews.resize(_swapChainImages.size());
	_reflectionImages.resize(2 * _swapChainImages.size());
	_reflectionImageViews.resize(2 * _swapChainImages.size());
	_reflectionIntermediateFilterImages.resize(_swapChainImages.size());
	_reflectionIntermediateFilterImageViews.resize(_swapChainImages.size());
	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		for(size_t j = 0; j < _gbufferSize; j++) {
			_gbufferImages[_gbufferSize * i + j].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
														VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
															VK_IMAGE_USAGE_SAMPLED_BIT);
			_gbufferImages[_gbufferSize * i + j].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			_gbufferImageViews[_gbufferSize * i + j].create(_device, _gbufferImages[_gbufferSize * i + j], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
			// Set initial layout to avoid errors when used in the UI even if we've never rendered to them
			_gbufferImages[_gbufferSize * i + j].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT,
																  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		_reflectionImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
									VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
										VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		_reflectionImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_reflectionImageViews[i].create(_device, _reflectionImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		// Set initial layout to avoid errors when used in the UI even if we've never rendered to them
		_reflectionImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
											  VK_IMAGE_LAYOUT_GENERAL);
		// Temp copies of previous frame
		auto tmpIndex = _swapChainImages.size() + i;
		_reflectionImages[tmpIndex].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
										   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		_reflectionImages[tmpIndex].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_reflectionImageViews[tmpIndex].create(_device, _reflectionImages[tmpIndex], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		_reflectionImages[tmpIndex].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
													 VK_IMAGE_LAYOUT_GENERAL);

		// FIXME: Review usage bits
		_directLightIntermediateFilterImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
													   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
														   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		_directLightIntermediateFilterImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_directLightIntermediateFilterImageViews[i].create(_device, _directLightIntermediateFilterImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		_directLightIntermediateFilterImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT,
																 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		_reflectionIntermediateFilterImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
													  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
														  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		_reflectionIntermediateFilterImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_reflectionIntermediateFilterImageViews[i].create(_device, _reflectionIntermediateFilterImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		_reflectionIntermediateFilterImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT,
																VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		_directLightImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
									 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
										 VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		_directLightImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_directLightImageViews[i].create(_device, _directLightImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		// Set initial layout to avoid errors when used in the UI even if we've never rendered to them
		_directLightImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
											   VK_IMAGE_LAYOUT_GENERAL);
		// Temp images from previous frame
		_directLightImages[tmpIndex].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
											VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		_directLightImages[tmpIndex].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_directLightImageViews[tmpIndex].create(_device, _directLightImages[tmpIndex], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		_directLightImages[tmpIndex].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
													  VK_IMAGE_LAYOUT_GENERAL);
	}
	_depthFormat = _physicalDevice.findSupportedFormat({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
													   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	_depthImage.create(_device, _swapChainExtent.width, _swapChainExtent.height, _depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	_depthImage.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_depthImageView.create(_device, _depthImage, _depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

	_mainTimingQueryPools.resize(_swapChainImages.size());
	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		_mainTimingQueryPools[i].create(_device, VK_QUERY_TYPE_TIMESTAMP, 7);
	}
}

void Editor::initUniformBuffers() {
	{
		// Enough buffers for current and previous frame value and each swapchain image.
		VkDeviceSize bufferSize = sizeof(CameraBuffer);
		_cameraUniformBuffers.resize(2 * _swapChainImages.size());
		for(size_t i = 0; i < 2 * _swapChainImages.size(); i++)
			_cameraUniformBuffers[i].create(_device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, bufferSize);
		auto   memReq = _cameraUniformBuffers[0].getMemoryRequirements();
		size_t memSize = _cameraUniformBuffers.size() * memReq.size; // (_swapChainImages.size() * (1 + bufferSize / memReq.alignment)) * memReq.alignment;
		_cameraUniformBuffersMemory.allocate(
			_device, _physicalDevice.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), memSize);
		size_t offset = 0;
		_uboStride = (1 + bufferSize / memReq.alignment) * memReq.alignment;
		for(size_t i = 0; i < _cameraUniformBuffers.size(); i++) {
			vkBindBufferMemory(_device, _cameraUniformBuffers[i], _cameraUniformBuffersMemory, offset);
			offset += _uboStride;
		}
	}
	{
		VkDeviceSize bufferSize = sizeof(LightBuffer);
		_lightUniformBuffers.resize(_swapChainImages.size());
		for(size_t i = 0; i < _swapChainImages.size(); i++)
			_lightUniformBuffers[i].create(_device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, bufferSize);
		auto   memReq = _lightUniformBuffers[0].getMemoryRequirements();
		size_t memSize = _swapChainImages.size() * memReq.size; //(_swapChainImages.size() * (1 + bufferSize / memReq.alignment)) * memReq.alignment;
		_lightUniformBuffersMemory.allocate(
			_device, _physicalDevice.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), memSize);
		size_t offset = 0;
		_lightUboStride = (1 + bufferSize / memReq.alignment) * memReq.alignment;
		for(size_t i = 0; i < _swapChainImages.size(); i++) {
			vkBindBufferMemory(_device, _lightUniformBuffers[i], _lightUniformBuffersMemory, offset);
			offset += _lightUboStride;
		}
	}

	for(uint32_t i = 0; i < _swapChainImages.size(); i++)
		updateUniformBuffer(i);
}

void Editor::initSwapChain() {
	// Generic DescriptorSetLayouts
	DescriptorSetLayoutBuilder builder; // Blue Noise DSL
	builder
		.add(VK_DESCRIPTOR_TYPE_SAMPLER,
			 VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) // Sampler
		.add(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
			 64); // Blue Noise Textures
	_descriptorSetLayouts.push_back(builder.build(_device));
	DescriptorPoolBuilder poolBuilder;
	poolBuilder.add(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1024)
		.add(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1024)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024)
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024)
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024);
	_descriptorPool = poolBuilder.build(_device, _swapChainImages.size() * _descriptorSetLayouts.size());
	std::vector<VkDescriptorSetLayout> layoutsToAllocate;
	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		for(size_t index = 0; index < _descriptorSetLayouts.size(); ++index) {
			layoutsToAllocate.push_back(_descriptorSetLayouts[index]);
		}
	}
	std::vector<VkDescriptorImageInfo> descInfos;
	for(size_t i = 0; i < 64; ++i)
		descInfos.push_back({
			.sampler = nullptr,
			.imageView = _blueNoiseTextures[i]->gpuImage->imageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		});
	_descriptorPool.allocate(layoutsToAllocate);
	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		DescriptorSetWriter dsw(_descriptorPool.getDescriptorSets()[i]);
		dsw.add(0, VK_DESCRIPTOR_TYPE_SAMPLER,
				{
					.sampler = *_blueNoiseTextures[0]->sampler,
					.imageView = VK_NULL_HANDLE,
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				});
		dsw.add(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, descInfos);
		dsw.update(_device);
	}

	initUniformBuffers();

	createGBufferPass();
	createGatherPass();
	createDirectLightPass();
	createReflectionPass();

	_commandBuffers.allocate(_device, _commandPool, _gbufferFramebuffers.size());
	_copyCommandBuffers.allocate(_device, _commandPool, _gbufferFramebuffers.size());

	_imagesInFlight.resize(_swapChainImages.size());

	createImGuiRenderPass();

	_presentFramebuffers.resize(_swapChainImageViews.size());
	for(size_t i = 0; i < _swapChainImageViews.size(); i++)
		_presentFramebuffers[i].create(_device, _imguiRenderPass, _swapChainImageViews[i], _swapChainExtent);

	// Raytracing
	_rayTraceCommandBuffers.allocate(_device, _commandPool, _swapChainImageViews.size());
	createStorageImage();
	createRayTracingPipeline();
	createRaytracingDescriptorSets();
	recordRayTracingCommands();

	// Irradiances Probes & Debug
	_irradianceProbes.createPipeline(_pipelineCache);
	_irradianceProbes.writeDescriptorSet(_renderer, _lightUniformBuffers[0]);
	_irradianceProbes.initProbes(_computeQueue);
	createProbeDebugPass();

	recordCommandBuffers();
}

void Editor::recordCommandBuffers() {
	for(size_t i = 0; i < _commandBuffers.getBuffers().size(); i++) {
		auto& b = _commandBuffers.getBuffers()[i];
		b.begin();
		_mainTimingQueryPools[i].reset(b);
		_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

		const std::vector<VkClearValue> clearValues{
			VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
			VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.depthStencil = {1.0f, 0}},
		};
		{
			b.beginRenderPass(_gbufferRenderPass, _gbufferFramebuffers[i], _swapChainExtent, clearValues);
			_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 1);
			_gbufferPipeline.bind(b);

			// Bind the instance data SSBO
			vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _gbufferPipeline.getLayout(), 1, 1,
									&_gbufferDescriptorPool.getDescriptorSets()[_commandBuffers.getBuffers().size() * Materials.size() + i], 0, nullptr);

			uint32_t indexCount = 0;
			uint32_t instanceCount = 0;
			uint32_t instanceBaseOffset = 0;
			{
				auto meshRenderers = _scene.getRegistry().view<MeshRendererComponent>(); // Pre-Sorted
				auto currentMaterial = InvalidMaterialIndex;
				auto currentMesh = InvalidMeshIndex;
				auto offsets = std::array<VkDeviceSize, 1>{0};
				for(const auto& entity : meshRenderers) {
					const auto& meshRenderer = _scene.getRegistry().get<MeshRendererComponent>(entity);
					if(meshRenderer.meshIndex != currentMesh) {
						// Issue a draw call for instanceCount instances before switching to the next mesh.
						if(currentMesh != InvalidMeshIndex) {
							vkCmdDrawIndexed(b, indexCount, instanceCount, 0, 0, instanceBaseOffset);
							instanceBaseOffset += instanceCount;
						}
						// Reset (Next Mesh)
						currentMesh = meshRenderer.meshIndex;
						instanceCount = 0;
						indexCount = static_cast<uint32_t>(_scene.getMeshes()[currentMesh].getIndices().size());
						std::array<VkBuffer, 1> buffers{_scene.getMeshes()[currentMesh].getVertexBuffer()};
						vkCmdBindVertexBuffers(b, 0, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
						vkCmdBindIndexBuffer(b, _scene.getMeshes()[currentMesh].getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
						// Count the instances before issuing a draw call.
					}
					if(meshRenderer.materialIndex != currentMaterial) {
						// Next Material
						vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _gbufferPipeline.getLayout(), 0, 1,
												&_gbufferDescriptorPool.getDescriptorSets()[i * Materials.size() + meshRenderer.materialIndex], 0, nullptr);
					}
					++instanceCount;
				}
				if(currentMesh != InvalidMeshIndex) {
					vkCmdDrawIndexed(b, indexCount, instanceCount, 0, 0, instanceBaseOffset);
					instanceBaseOffset += instanceCount;
				}
			}
			// NOTE: We can't batch calls for skinned meshes, vertex buffers have to be updated for the raytracing pass, we'll reuse them directly.
			{
				auto					currentMaterial = InvalidMaterialIndex;
				auto					currentMesh = InvalidMeshIndex;
				auto					skinnedMeshRenderers = _scene.getRegistry().view<SkinnedMeshRendererComponent>();
				std::array<VkBuffer, 1> buffers{_renderer.Vertices.buffer()};
				for(const auto& entity : skinnedMeshRenderers) {
					const auto& meshRenderer = _scene.getRegistry().get<SkinnedMeshRendererComponent>(entity);
					if(meshRenderer.meshIndex != currentMesh) {
						currentMesh = meshRenderer.meshIndex;
						vkCmdBindIndexBuffer(b, _scene.getMeshes()[currentMesh].getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32); // Index buffer doesn't have to be updated.
						indexCount = static_cast<uint32_t>(_scene.getMeshes()[currentMesh].getIndices().size());
					}
					if(meshRenderer.materialIndex != currentMaterial) {
						// Next Material
						vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _gbufferPipeline.getLayout(), 0, 1,
												&_gbufferDescriptorPool.getDescriptorSets()[i * Materials.size() + meshRenderer.materialIndex], 0, nullptr);
					}

#if 0 
					auto offsets = std::array<VkDeviceSize, 1>{
						_scene._dynamicOffsetTable[meshRenderer.indexIntoOffsetTable - _scene.StaticOffsetTableSizeInBytes / sizeof(Scene::OffsetEntry)].vertexOffset *
						sizeof(Vertex)}; // Use the vertex buffer of this particular instance.

					vkCmdBindVertexBuffers(b, 0, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
					vkCmdDrawIndexed(b, indexCount, 1, 0, 0, instanceBaseOffset);
#else
					auto offsets = std::array<VkDeviceSize, 1>{0};
					vkCmdBindVertexBuffers(b, 0, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
					vkCmdDrawIndexed(
						b, indexCount, 1, 0,
						_renderer.getDynamicOffsetTable()[meshRenderer.indexIntoOffsetTable - _renderer.StaticOffsetTableSizeInBytes / sizeof(Renderer::OffsetEntry)].vertexOffset,
						instanceBaseOffset);
#endif
					++instanceBaseOffset;
				}
			}
			_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 2);
			b.endRenderPass();

			auto wholeImage = VkImageSubresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			};

			// Trace reflections if enabled
			if(_enableReflections) {
				vkCmdBindPipeline(b, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _reflectionPipeline);
				const auto descriptors = {_reflectionDescriptorPool.getDescriptorSets()[i], _descriptorPool.getDescriptorSets()[i]};
				vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _reflectionPipeline.getLayout(), 0, descriptors.size(), descriptors.begin(), 0, 0);
				vkCmdTraceRaysKHR(b, &_reflectionShaderBindingTable.raygenEntry, &_reflectionShaderBindingTable.missEntry, &_reflectionShaderBindingTable.anyhitEntry,
								  &_reflectionShaderBindingTable.callableEntry, _width, _height, 1);
			}
			// Trace direct light (Currently only the sun)
			{
				vkCmdBindPipeline(b, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _directLightPipeline);
				const auto descriptors = {_directLightDescriptorPool.getDescriptorSets()[i], _descriptorPool.getDescriptorSets()[i]};
				vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _directLightPipeline.getLayout(), 0, descriptors.size(), descriptors.begin(), 0, 0);
				vkCmdTraceRaysKHR(b, &_directLightShaderBindingTable.raygenEntry, &_directLightShaderBindingTable.missEntry, &_directLightShaderBindingTable.anyhitEntry,
								  &_directLightShaderBindingTable.callableEntry, _width, _height, 1);
			}
			_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 3);

			// Filter Direct Light & Reflections (Not physically based)
			const auto groupSize = 32;
			glm::ivec2 launchSize = {glm::ceil(static_cast<float>(_width) / groupSize), glm::ceil(static_cast<float>(_height) / groupSize)};

			_directLightFilterPipelineX.bind(b, VK_PIPELINE_BIND_POINT_COMPUTE);
			_directLightImages[i].barrier(b, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
										  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
			_directLightIntermediateFilterImages[i].barrier(b, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
															VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
			_directLightImages[i + _swapChainImages.size()].barrier(b, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_TRANSFER_WRITE_BIT,
																	VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
			vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_COMPUTE, _directLightFilterPipelineX.getLayout(), 0, 1,
									&_directLightFilterDescriptorPool.getDescriptorSets()[2 * i + 0], 0, 0);
			vkCmdDispatch(b, launchSize.x, launchSize.y, 1);
			_directLightIntermediateFilterImages[i].barrier(b, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
															VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
			_directLightFilterPipelineY.bind(b, VK_PIPELINE_BIND_POINT_COMPUTE);
			vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_COMPUTE, _directLightFilterPipelineY.getLayout(), 0, 1,
									&_directLightFilterDescriptorPool.getDescriptorSets()[2 * i + 1], 0, 0);
			vkCmdDispatch(b, launchSize.x, launchSize.y, 1);

			if(_enableReflections) {
				// Wait on copy from another command buffer (reflections from previous frame)
				_reflectionImages[i + _swapChainImages.size()].barrier(b, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
																	   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
				_reflectionFilterPipelineX.bind(b, VK_PIPELINE_BIND_POINT_COMPUTE);
				vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_COMPUTE, _reflectionFilterPipelineX.getLayout(), 0, 1,
										&_reflectionFilterDescriptorPool.getDescriptorSets()[2 * i + 0], 0, 0);
				vkCmdDispatch(b, launchSize.x, launchSize.y, 1);
				_reflectionIntermediateFilterImages[i].barrier(b, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
															   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
				_reflectionFilterPipelineY.bind(b, VK_PIPELINE_BIND_POINT_COMPUTE);
				vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_COMPUTE, _reflectionFilterPipelineY.getLayout(), 0, 1,
										&_reflectionFilterDescriptorPool.getDescriptorSets()[2 * i + 1], 0, 0);
				vkCmdDispatch(b, launchSize.x, launchSize.y, 1);

				_reflectionImages[i].barrier(b, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
											 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
			}

			_directLightImages[i].barrier(b, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
										  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

			// Gather
			{
				const std::vector<VkClearValue> clearValues{
					VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
					VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.depthStencil = {1.0f, 0}},
				};
				b.beginRenderPass(_gatherRenderPass, _gatherFramebuffers[i], _swapChainExtent, clearValues);
				_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 4);
				_gatherPipeline.bind(b);
				vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _gatherPipeline.getLayout(), 0, 1, &_gatherDescriptorPool.getDescriptorSets()[i], 0, nullptr);
				vkCmdDraw(b, 3, 1, 0, 0);
				_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 5);
				b.endRenderPass();
			}

			// Probes Debug
			if(_probeDebug) {
				const std::vector<VkClearValue> clearValues{
					VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
					VkClearValue{.depthStencil = {1.0f, 0}},
				};
				b.beginRenderPass(_probeDebugRenderPass, _probeDebugFramebuffers[i], _swapChainExtent, clearValues);
				_probeDebugPipeline.bind(b);
				vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _probeDebugPipeline.getLayout(), 0, 1, &_probeDebugDescriptorPool.getDescriptorSets()[i], 0, nullptr);

				const auto&	 m = _probeMesh.getMeshes()[0];
				VkDeviceSize offsets[1] = {0};
				vkCmdBindVertexBuffers(b, 0, 1, &m.getVertexBuffer().getHandle(), offsets);
				vkCmdBindIndexBuffer(b, m.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(b, static_cast<uint32_t>(m.getIndices().size()),
								 _irradianceProbes.GridParameters.resolution.x * _irradianceProbes.GridParameters.resolution.y * _irradianceProbes.GridParameters.resolution.z, 0,
								 0, 0);
				b.endRenderPass();
			}

			_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 6);
			b.end();
		}
	}
}

void Editor::recreateSwapChain() {
	int width = 0, height = 0;
	glfwGetFramebufferSize(_window, &width, &height);
	while(width == 0 || height == 0) {
		glfwGetFramebufferSize(_window, &width, &height);
		glfwWaitEvents();
	}

	VK_CHECK(vkDeviceWaitIdle(_device));

	cleanupSwapChain();

	createSwapChain();
	initSwapChain();
	uiOnTextureChange();
}

void Editor::cleanupSwapChain() {
	_mainTimingQueryPools.clear();

	_rayTraceCommandBuffers.free();
	destroyRayTracingPipeline();
	_rayTraceStorageImageViews.clear();
	_rayTraceStorageImages.clear();

	_presentFramebuffers.clear();
	_imguiCommandBuffers.free();
	_imguiRenderPass.destroy();

	_probeDebugFramebuffers.clear();
	_probeDebugRenderPass.destroy();
	_probeDebugDescriptorPool.destroy();
	_probeDebugDescriptorSetLayouts.clear();
	_probeDebugPipeline.destroy();

	destroyGBufferPipeline();
	destroyDirectLightPipeline();
	destroyReflectionPipeline();

	_cameraUniformBuffers.clear();
	_cameraUniformBuffersMemory.free();
	_lightUniformBuffers.clear();
	_lightUniformBuffersMemory.free();
	_descriptorPool.destroy();
	_gbufferFramebuffers.clear();
	destroyGatherPass();

	// Only free up the command buffer, not the command pool
	_commandBuffers.free();
	_copyCommandBuffers.free();

	_descriptorSetLayouts.clear();

	_gbufferRenderPass.destroy();

	_gbufferImages.clear();
	_gbufferImageViews.clear();
	_directLightImages.clear();
	_directLightImageViews.clear();
	_directLightIntermediateFilterImages.clear();
	_directLightIntermediateFilterImageViews.clear();
	_reflectionImages.clear();
	_reflectionImageViews.clear();
	_reflectionIntermediateFilterImages.clear();
	_reflectionIntermediateFilterImageViews.clear();
	_depthImageView.destroy();
	_depthImage.destroy();
	_swapChainImageViews.clear();

	vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}
