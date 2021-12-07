#include <Application.hpp>

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for(VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

		if(tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		} else if(tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}

	throw std::runtime_error("Failed to find supported format.");
}

VkSurfaceFormatKHR Application::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
	for(const auto& availableFormat : availableFormats) {
		// FIXME: Imgui windows don't look right when VK_COLOR_SPACE_SRGB_NONLINEAR_KHR is used.
		// if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
		if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
			return availableFormat;
		}
	}

	return availableFormats[0];
}

VkPresentModeKHR Application::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
	for(const auto& availablePresentMode : availablePresentModes) {
		if(availablePresentMode == _preferedPresentMode) {
			return availablePresentMode;
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Application::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
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

void Application::createSwapChain() {
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

	_depthFormat = findSupportedFormat(_physicalDevice, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
									   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	_depthImage.create(_device, _swapChainExtent.width, _swapChainExtent.height, _depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	_depthImage.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_depthImageView.create(_device, _depthImage, _depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Application::initSwapChain() {
	_renderPass.create(_device, _swapChainImageFormat, _depthFormat);

	VkAttachmentReference colorAttachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	_imguiRenderPass.create(_device,
							std::array<VkAttachmentDescription, 1>{VkAttachmentDescription{
								.format = _swapChainImageFormat,
								.samples = VK_SAMPLE_COUNT_1_BIT,
								.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
								.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
								.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
								.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
								.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
								.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
							}},
							std::array<VkSubpassDescription, 1>{VkSubpassDescription{
								.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
								.colorAttachmentCount = 1,
								.pColorAttachments = &colorAttachment,
							}},
							std::array<VkSubpassDependency, 1>{VkSubpassDependency{
								.srcSubpass = VK_SUBPASS_EXTERNAL,
								.dstSubpass = 0,
								.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
								.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
								.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
								.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
							}});

	Shader vertShader(_device, "./shaders_spv/ubo.vert.spv");
	Shader fragShader(_device, "./shaders_spv/phong.frag.spv");

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
		vertShader.getStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
		fragShader.getStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	DescriptorSetLayoutBuilder builder;
	builder.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
	_descriptorSetLayouts.push_back(builder.build(_device));

	std::vector<VkDescriptorSetLayout> layouts;
	for(const auto& layout : _descriptorSetLayouts)
		layouts.push_back(layout);

	_pipeline.create(_device, shaderStages, _renderPass, _swapChainExtent, layouts, _pipelineCache);

	_swapChainFramebuffers.resize(_swapChainImageViews.size());
	for(size_t i = 0; i < _swapChainImageViews.size(); i++)
		_swapChainFramebuffers[i].create(_device, _renderPass, {_swapChainImageViews[i], _depthImageView}, _swapChainExtent);

	_imguiFramebuffers.resize(_swapChainImageViews.size());
	for(size_t i = 0; i < _swapChainImageViews.size(); i++) {
		_imguiFramebuffers[i].create(_device, _imguiRenderPass, _swapChainImageViews[i], _swapChainExtent);
	}

	_commandBuffers.allocate(_device, _commandPool, _swapChainFramebuffers.size());
	_imguiCommandBuffers.allocate(_device, _imguiCommandPool, _swapChainFramebuffers.size());

	_imagesInFlight.resize(_swapChainImages.size());

	// Uniform buffers init
	{
		VkDeviceSize bufferSize = sizeof(UniformBufferObject);

		_uniformBuffers.resize(_swapChainImages.size());

		for(size_t i = 0; i < _swapChainImages.size(); i++)
			_uniformBuffers[i].create(_device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, bufferSize);

		auto   memReq = _uniformBuffers[0].getMemoryRequirements();
		size_t memSize = (2 + _swapChainImages.size() * bufferSize / memReq.alignment) * memReq.alignment;
		_uniformBuffersMemory.allocate(_device, _physicalDevice.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
									   memSize);
		size_t offset = 0;
		for(size_t i = 0; i < _swapChainImages.size(); i++) {
			vkBindBufferMemory(_device, _uniformBuffers[i], _uniformBuffersMemory, offset);
			offset += bufferSize;
			offset = (1 + offset / memReq.alignment) * memReq.alignment;
		}
	}

	uint32_t			  descriptorSetsCount = _swapChainImages.size() * Materials.size();
	DescriptorPoolBuilder poolBuilder;
	poolBuilder.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorSetsCount);
	poolBuilder.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * descriptorSetsCount);
	_descriptorPool = poolBuilder.build(_device, descriptorSetsCount);

	std::vector<VkDescriptorSetLayout> descriptorSetsLayoutsToAllocate;
	for(size_t i = 0; i < _swapChainImages.size(); ++i)
		for(const auto& material : Materials) {
			descriptorSetsLayoutsToAllocate.push_back(_descriptorSetLayouts[0]);
		}
	_descriptorPool.allocate(descriptorSetsLayoutsToAllocate);

	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		for(size_t m = 0; m < Materials.size(); m++) {
			VkDescriptorBufferInfo bufferInfo{
				.buffer = _uniformBuffers[i],
				.offset = 0,
				.range = sizeof(UniformBufferObject),
			};
			VkDescriptorImageInfo imageInfo{
				.sampler = *Textures[Materials[m].albedoTexture].sampler,
				.imageView = Textures[Materials[m].albedoTexture].gpuImage->imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			std::vector<VkWriteDescriptorSet> descriptorWrites;

			descriptorWrites.push_back(VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = _descriptorPool.getDescriptorSets()[i * Materials.size() + m],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pImageInfo = nullptr,
				.pBufferInfo = &bufferInfo,
				.pTexelBufferView = nullptr,
			});
			descriptorWrites.push_back(VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = _descriptorPool.getDescriptorSets()[i * Materials.size() + m],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &imageInfo,
				.pBufferInfo = nullptr,
				.pTexelBufferView = nullptr,
			});
			// Use a blank texture if this mesh doesn't have a normal map
			auto&				  normals = Materials[m].normalTexture != -1 ? Textures[Materials[m].normalTexture] : _blankTexture;
			VkDescriptorImageInfo normalInfo{
				.sampler = *normals.sampler,
				.imageView = normals.gpuImage->imageView,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			descriptorWrites.push_back(VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = _descriptorPool.getDescriptorSets()[i * Materials.size() + m],
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &normalInfo,
				.pBufferInfo = nullptr,
				.pTexelBufferView = nullptr,
			});

			vkUpdateDescriptorSets(_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		}
	}

	recordCommandBuffers();

	_rayTraceCommandBuffers.allocate(_device, _commandPool, _swapChainFramebuffers.size());
	createStorageImage();
	createRayTracingPipeline();
	createRaytracingDescriptorSets();
	recordRayTracingCommands();
}

void Application::recordCommandBuffers() {
	for(size_t i = 0; i < _commandBuffers.getBuffers().size(); i++) {
		auto& b = _commandBuffers.getBuffers()[i];
		b.begin();
		b.beginRenderPass(_renderPass, _swapChainFramebuffers[i], _swapChainExtent);
		_pipeline.bind(b);

		// Trying to regroup meshses by material to gain some perfomance: No gain :^)
		for(size_t mIdx = 0; mIdx < Materials.size(); ++mIdx) {
			vkCmdBindDescriptorSets(_commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.getLayout(), 0, 1,
									&_descriptorPool.getDescriptorSets()[i * Materials.size() + mIdx], 0, nullptr);
			for(const auto& m : _scene.getMeshes()) {
				if(m.materialIndex == mIdx) {
					_commandBuffers[i].bind<1>({m.getVertexBuffer()});
					vkCmdBindIndexBuffer(_commandBuffers[i], m.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
					vkCmdDrawIndexed(_commandBuffers[i], static_cast<uint32_t>(m.getIndices().size()), 1, 0, 0, 0);
				}
			}
		}

		b.endRenderPass();
		b.end();
	}
}

void Application::recreateSwapChain() {
	int width = 0, height = 0;
	glfwGetFramebufferSize(_window, &width, &height);
	while(width == 0 || height == 0) {
		glfwGetFramebufferSize(_window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(_device);

	cleanupSwapChain();

	createSwapChain();
	initSwapChain();
}

void Application::cleanupSwapChain() {
	_rayTraceCommandBuffers.free();
	_rayTracingPipeline.destroy();
	_rayTracingDescriptorPool.destroy();
	_rayTracingDescriptorSetLayout.destroy();
	_rayTracingPipelineLayout.destroy();
	_rayTraceStorageImageView.destroy();
	_rayTraceStorageImage.destroy();

	_imguiFramebuffers.clear();
	_imguiCommandBuffers.free();
	_imguiRenderPass.destroy();

	for(auto& b : _uniformBuffers)
		b.destroy();
	_uniformBuffersMemory.free();
	_descriptorPool.destroy();
	_swapChainFramebuffers.clear();

	// Only free up the command buffer, not the command pool
	_commandBuffers.free();
	_pipeline.destroy();
	_descriptorSetLayouts.clear();
	_renderPass.destroy();
	_depthImageView.destroy();
	_depthImage.destroy();
	_swapChainImageViews.clear();
	vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}
