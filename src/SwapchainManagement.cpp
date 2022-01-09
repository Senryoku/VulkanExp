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
		if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			// if(availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
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

	VK_CHECK(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapChain));

	vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, nullptr);
	_swapChainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, _swapChainImages.data());

	_swapChainImageFormat = surfaceFormat.format;
	_swapChainExtent = extent;

	for(size_t i = 0; i < _swapChainImages.size(); i++)
		_swapChainImageViews.push_back(ImageView{_device, _swapChainImages[i], _swapChainImageFormat});

	// Create GBuffer Images & Views
	_gbufferImages.resize(3 * _swapChainImages.size());
	_gbufferImageViews.resize(3 * _swapChainImages.size());
	_reflectionImages.resize(_swapChainImages.size());
	_reflectionImageViews.resize(_swapChainImages.size());
	_reflectionFilteredImages.resize(_swapChainImages.size());
	_reflectionFilteredImageViews.resize(_swapChainImages.size());
	_reflectionMipmapImageViews.resize(_swapChainImages.size());
	_directLightImages.resize(_swapChainImages.size());
	_directLightImageViews.resize(_swapChainImages.size());
	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		for(size_t j = 0; j < 3; j++) {
			_gbufferImages[3 * i + j].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
											 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			_gbufferImages[3 * i + j].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			_gbufferImageViews[3 * i + j].create(_device, _gbufferImages[3 * i + j], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
			// Set initial layout to avoid errors when used in the UI even if we've never rendered to them
			_gbufferImages[3 * i + j].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
													   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		_reflectionImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
									VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
										VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
									true);
		_reflectionImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		// View into the first mipmap only for framebuffer usage
		_reflectionImageViews[i].create(
			_device,
			{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			 .image = _reflectionImages[i],
			 .viewType = VK_IMAGE_VIEW_TYPE_2D,
			 .format = VK_FORMAT_R32G32B32A32_SFLOAT,
			 .components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY, .g = VK_COMPONENT_SWIZZLE_IDENTITY, .b = VK_COMPONENT_SWIZZLE_IDENTITY, .a = VK_COMPONENT_SWIZZLE_IDENTITY},
			 .subresourceRange = {
				 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				 .baseMipLevel = 0,
				 .levelCount = 1,
				 .baseArrayLayer = 0,
				 .layerCount = 1,
			 }});
		_reflectionMipmapImageViews[i].create(_device, _reflectionImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		// Set initial layout to avoid errors when used in the UI even if we've never rendered to them
		_reflectionImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
											  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// FIXME: Review usage bits
		_reflectionFilteredImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
											VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
												VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		_reflectionFilteredImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_reflectionFilteredImageViews[i].create(_device, _reflectionFilteredImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		_reflectionFilteredImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
													  VK_IMAGE_LAYOUT_GENERAL);

		_directLightImages[i].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
									 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		_directLightImages[i].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		_directLightImageViews[i].create(_device, _directLightImages[i], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		// Set initial layout to avoid errors when used in the UI even if we've never rendered to them
		_directLightImages[i].transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED,
											   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	_depthFormat = findSupportedFormat(_physicalDevice, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
									   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	_depthImage.create(_device, _swapChainExtent.width, _swapChainExtent.height, _depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	_depthImage.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_depthImageView.create(_device, _depthImage, _depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

	_mainTimingQueryPools.resize(_swapChainImages.size());
	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		_mainTimingQueryPools[i].create(_device, VK_QUERY_TYPE_TIMESTAMP, 6);
	}
}

void Application::initUniformBuffers() {
	{
		VkDeviceSize bufferSize = sizeof(CameraBuffer);
		_cameraUniformBuffers.resize(_swapChainImages.size());
		for(size_t i = 0; i < _swapChainImages.size(); i++)
			_cameraUniformBuffers[i].create(_device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, bufferSize);
		auto   memReq = _cameraUniformBuffers[0].getMemoryRequirements();
		size_t memSize = _swapChainImages.size() * memReq.size; // (_swapChainImages.size() * (1 + bufferSize / memReq.alignment)) * memReq.alignment;
		_cameraUniformBuffersMemory.allocate(
			_device, _physicalDevice.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), memSize);
		size_t offset = 0;
		_uboStride = (1 + bufferSize / memReq.alignment) * memReq.alignment;
		for(size_t i = 0; i < _swapChainImages.size(); i++) {
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

void Application::initProbeDebug() {
	Shader probeVertShader(_device, "./shaders_spv/probe_instanced.vert.spv");
	Shader probeFragShader(_device, "./shaders_spv/probe_debug.frag.spv");

	std::vector<VkPipelineShaderStageCreateInfo> probeShaderStages{
		probeVertShader.getStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
		probeFragShader.getStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	DescriptorSetLayoutBuilder builder;
	builder
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)								   // Camera
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) // Grid Parameters
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)								   // Probes Info
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)					   // Probes Color
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);					   // Probes Depth

	_probeDebugDescriptorSetLayouts.push_back(builder.build(_device));

	std::vector<VkDescriptorSetLayout> probeLayouts;
	for(const auto& layout : _probeDebugDescriptorSetLayouts)
		probeLayouts.push_back(layout);

	VkAttachmentReference colorAttachmentRef{
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkAttachmentReference depthAttachmentRef{
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	_probeDebugRenderPass.create(_device,
								 std::array<VkAttachmentDescription, 2>{VkAttachmentDescription{
																			.format = _swapChainImageFormat,
																			.samples = VK_SAMPLE_COUNT_1_BIT,
																			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
																			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
																			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
																			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
																			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
																			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
																		},
																		VkAttachmentDescription{
																			.format = _depthFormat,
																			.samples = VK_SAMPLE_COUNT_1_BIT,
																			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
																			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
																			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
																			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
																			.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
																			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
																		}},
								 std::array<VkSubpassDescription, 1>{VkSubpassDescription{
									 .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
									 .colorAttachmentCount = 1,
									 .pColorAttachments = &colorAttachmentRef,
									 .pDepthStencilAttachment = &depthAttachmentRef,
								 }},
								 std::array<VkSubpassDependency, 1>{VkSubpassDependency{
									 .srcSubpass = VK_SUBPASS_EXTERNAL,
									 .dstSubpass = 0,
									 .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
									 .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
									 .srcAccessMask = 0,
									 .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
								 }});

	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &bindingDescription, // Optional
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
		.pVertexAttributeDescriptions = attributeDescriptions.data(), // Optional
	};

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	VkViewport viewport{
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)_swapChainExtent.width,
		.height = (float)_swapChainExtent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor{
		.offset = {0, 0},
		.extent = _swapChainExtent,
	};

	VkPipelineViewportStateCreateInfo viewportState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor,
	};

	VkPipelineRasterizationStateCreateInfo rasterizer{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0.0f, // Optional
		.depthBiasClamp = 0.0f,			 // Optional
		.depthBiasSlopeFactor = 0.0f,	 // Optional
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisampling{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
		.minSampleShading = 1.0f,		   // Optional
		.pSampleMask = nullptr,			   // Optional
		.alphaToCoverageEnable = VK_FALSE, // Optional
		.alphaToOneEnable = VK_FALSE,	   // Optional
	};

	VkPipelineColorBlendAttachmentState colorBlendAttachment{
		.blendEnable = VK_FALSE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,	 // Optional
		.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO, // Optional
		.colorBlendOp = VK_BLEND_OP_ADD,			 // Optional
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,	 // Optional
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, // Optional
		.alphaBlendOp = VK_BLEND_OP_ADD,			 // Optional
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineDepthStencilStateCreateInfo depthStencil{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
		.front = {},			// Optional
		.back = {},				// Optional
		.minDepthBounds = 0.0f, // Optional
		.maxDepthBounds = 1.0f, // Optional
	};

	VkPipelineColorBlendStateCreateInfo colorBlending{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY, // Optional
		.attachmentCount = 1,
		.pAttachments = &colorBlendAttachment,
		.blendConstants = {0.0f, 0.0f, 0.0f, 0.0f} // Optional
	};

	VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH};

	VkPipelineDynamicStateCreateInfo dynamicState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStates,
	};

	_probeDebugPipeline.getLayout().create(_device, probeLayouts);

	VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<uint32_t>(probeShaderStages.size()),
		.pStages = probeShaderStages.data(),
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssembly,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pDepthStencilState = &depthStencil, // Optional
		.pColorBlendState = &colorBlending,
		.pDynamicState = nullptr, // Optional
		.layout = _probeDebugPipeline.getLayout(),
		.renderPass = _probeDebugRenderPass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE, // Optional
		.basePipelineIndex = -1,			  // Optional
	};

	_probeDebugPipeline.create(_device, pipelineInfo, _pipelineCache);

	std::vector<VkDescriptorSetLayout> descriptorSetsLayoutsToAllocate;
	for(size_t i = 0; i < _swapChainImages.size(); ++i)
		descriptorSetsLayoutsToAllocate.push_back(_probeDebugDescriptorSetLayouts[0]);
	DescriptorPoolBuilder poolBuilder;
	poolBuilder.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * static_cast<uint32_t>(_swapChainImages.size()));
	poolBuilder.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * static_cast<uint32_t>(_swapChainImages.size()));
	_probeDebugDescriptorPool = poolBuilder.build(_device, static_cast<uint32_t>(_swapChainImages.size()));
	_probeDebugDescriptorPool.allocate(descriptorSetsLayoutsToAllocate);
	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		DescriptorSetWriter dsw(_probeDebugDescriptorPool.getDescriptorSets()[i]);
		dsw.add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					.buffer = _cameraUniformBuffers[i],
					.offset = 0,
					.range = sizeof(CameraBuffer),
				});
		dsw.add(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					.buffer = _irradianceProbes.getGridParametersBuffer(),
					.offset = 0,
					.range = sizeof(IrradianceProbes::GridInfo),
				});
		dsw.add(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				{
					.buffer = _irradianceProbes.getProbeInfoBuffer(),
					.offset = 0,
					.range = VK_WHOLE_SIZE,
				});
		dsw.add(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					.sampler = *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
										   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0),
					.imageView = _irradianceProbes.getColorView(),
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				});
		dsw.add(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					.sampler = *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
										   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0),
					.imageView = _irradianceProbes.getDepthView(),
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				});
		dsw.update(_device);
	}

	_probeDebugFramebuffers.resize(_swapChainImageViews.size());
	for(size_t i = 0; i < _swapChainImageViews.size(); i++)
		_probeDebugFramebuffers[i].create(_device, _probeDebugRenderPass, {_swapChainImageViews[i], _depthImageView}, _swapChainExtent);
}

void Application::initSwapChain() {
	{
		RenderPassBuilder rpb;
		// Attachments
		rpb.add({
					.format = VK_FORMAT_R32G32B32A32_SFLOAT,
					.samples = VK_SAMPLE_COUNT_1_BIT,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
					.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.finalLayout = VK_IMAGE_LAYOUT_GENERAL,
				})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_GENERAL,
			})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_GENERAL,
			})
			.add({
				.format = _depthFormat,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			})
			.addSubPass(VK_PIPELINE_BIND_POINT_GRAPHICS,
						{// Output (GBuffer)
						 {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
						 {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
						 {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
						{}, {},
						{// Depth
						 3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
						{})
			// Dependencies (FIXME!)
			.add({
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			});
		_gbufferRenderPass = rpb.build(_device);

		_gbufferFramebuffers.resize(_swapChainImageViews.size());
		for(size_t i = 0; i < _swapChainImageViews.size(); i++)
			_gbufferFramebuffers[i].create(_device, _gbufferRenderPass,
										   {
											   _gbufferImageViews[3 * i + 0],
											   _gbufferImageViews[3 * i + 1],
											   _gbufferImageViews[3 * i + 2],
											   _depthImageView,
										   },
										   _swapChainExtent);
	}
	{
		RenderPassBuilder rpb;
		// Attachments
		rpb.add({
					.format = VK_FORMAT_R32G32B32A32_SFLOAT,
					.samples = VK_SAMPLE_COUNT_1_BIT,
					.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
					.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
					.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
					.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
					.initialLayout = VK_IMAGE_LAYOUT_GENERAL,
					.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_GENERAL,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_GENERAL,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			})
			.add({
				.format = _swapChainImageFormat,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			})
			.addSubPass(VK_PIPELINE_BIND_POINT_GRAPHICS,
						{
							// Output
							{4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
						},
						{
							// Inputs (GBuffer)
							{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
							{1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
							{2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
							{3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, // Direct Light
						},
						{},
						// Depth
						{VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}, {})
			// Dependencies (FIXME! I have no idea what I'm doing)
			.add({
				// Input Deps (?)
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
			})
			.add({
				// Output Deps (?)
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			});
		_gatherRenderPass = rpb.build(_device);

		_gatherFramebuffers.resize(_swapChainImageViews.size());
		for(size_t i = 0; i < _swapChainImageViews.size(); i++)
			_gatherFramebuffers[i].create(_device, _gatherRenderPass,
										  {
											  _gbufferImageViews[3 * i + 0],
											  _gbufferImageViews[3 * i + 1],
											  _gbufferImageViews[3 * i + 2],
											  _directLightImageViews[i],
											  _swapChainImageViews[i],
										  },
										  _swapChainExtent);
	}

	initUniformBuffers();

	createGBufferPipeline();
	createGatherPipeline();
	createReflectionShadowPipeline();

	_commandBuffers.allocate(_device, _commandPool, _gbufferFramebuffers.size());

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
	_irradianceProbes.createPipeline();
	_irradianceProbes.writeDescriptorSet(_scene, _topLevelAccelerationStructure, _lightUniformBuffers[0]);
	_irradianceProbes.initProbes(_computeQueue);
	initProbeDebug();

	recordCommandBuffers();
}

void Application::recordCommandBuffers() {
	for(size_t i = 0; i < _commandBuffers.getBuffers().size(); i++) {
		auto& b = _commandBuffers.getBuffers()[i];
		b.begin();
		_mainTimingQueryPools[i].reset(b);
		_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

		const std::vector<VkClearValue> clearValues{
			VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
			VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
			VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
			VkClearValue{.depthStencil = {1.0f, 0}},
		};
		{
			b.beginRenderPass(_gbufferRenderPass, _gbufferFramebuffers[i], _swapChainExtent, clearValues);
			_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 1);
			_gbufferPipeline.bind(b);

			const std::function<void(const Scene::Node&, glm::mat4)> visitNode = [&](const Scene::Node& n, glm::mat4 transform) {
				transform = transform * n.transform;
				if(n.mesh != -1) {
					GBufferPushConstant pc{transform, 0, 0};
					for(const auto& submesh : _scene.getMeshes()[n.mesh].SubMeshes) {
						pc.metalness = submesh.material->metallicFactor;
						pc.roughness = submesh.material->roughnessFactor;
						vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _gbufferPipeline.getLayout(), 0, 1,
												&_gbufferDescriptorPool.getDescriptorSets()[i * Materials.size() + submesh.materialIndex], 0, nullptr);
						vkCmdPushConstants(b, _gbufferPipeline.getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GBufferPushConstant), &pc);
						b.bind<1>({submesh.getVertexBuffer()});
						vkCmdBindIndexBuffer(b, submesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
						vkCmdDrawIndexed(b, static_cast<uint32_t>(submesh.getIndices().size()), 1, 0, 0, 0);
					}
				}

				for(const auto& c : n.children)
					visitNode(_scene.getNodes()[c], transform);
			};
			visitNode(_scene.getRoot(), glm::mat4(1.0f));

			_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 2);
			b.endRenderPass();
		}
		/*
		// Is it really necessary?
		std::vector<VkImageMemoryBarrier> barriers{{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
													.pNext = VK_NULL_HANDLE,
													.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
													.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
													.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
													.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
													.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
													.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
													.image = _gbufferImages[3 * i + 0],
													.subresourceRange =
														VkImageSubresourceRange{
															.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
															.baseMipLevel = 0,
															.levelCount = 1,
															.baseArrayLayer = 0,
															.layerCount = 1,
														}},
												   {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
													.pNext = VK_NULL_HANDLE,
													.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
													.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
													.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
													.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
													.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
													.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
													.image = _gbufferImages[3 * i + 1],
													.subresourceRange =
														VkImageSubresourceRange{
															.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
															.baseMipLevel = 0,
															.levelCount = 1,
															.baseArrayLayer = 0,
															.layerCount = 1,
														}},
												   {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
													.pNext = VK_NULL_HANDLE,
													.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
													.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
													.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
													.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
													.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
													.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
													.image = _gbufferImages[3 * i + 2],
													.subresourceRange =
														VkImageSubresourceRange{
															.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
															.baseMipLevel = 0,
															.levelCount = 1,
															.baseArrayLayer = 0,
															.layerCount = 1,
														}},
												   {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
													.pNext = VK_NULL_HANDLE,
													.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
													.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
													.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
													.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
													.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
													.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
													.image = _depthImage,
													.subresourceRange = VkImageSubresourceRange{
														.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
														.baseMipLevel = 0,
														.levelCount = 1,
														.baseArrayLayer = 0,
														.layerCount = 1,
													}}};
		vkCmdPipelineBarrier(b, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
							 static_cast<uint32_t>(barriers.size()), barriers.data());
		*/

		// TODO: Compute reflection & Shadow via Ray Tracing

		Image::setLayout(b, _reflectionImages[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
						 VkImageSubresourceRange{
							 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							 .baseMipLevel = 0,
							 .levelCount = 1, // FIXME: Update when we actually compute those
							 .baseArrayLayer = 0,
							 .layerCount = 1,
						 });

		Image::setLayout(b, _directLightImages[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
						 VkImageSubresourceRange{
							 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							 .baseMipLevel = 0,
							 .levelCount = 1,
							 .baseArrayLayer = 0,
							 .layerCount = 1,
						 });

		vkCmdBindPipeline(b, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _reflectionShadowPipeline);
		vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _reflectionShadowPipeline.getLayout(), 0, 1, &_reflectionShadowDescriptorPool.getDescriptorSets()[i], 0,
								0);
		vkCmdTraceRaysKHR(b, &_reflectionShadowShaderBindingTable.raygenEntry, &_reflectionShadowShaderBindingTable.missEntry, &_reflectionShadowShaderBindingTable.anyhitEntry,
						  &_reflectionShadowShaderBindingTable.callableEntry, _width, _height, 1);

		// Filter Reflections
		_reflectionFilterPipeline.bind(b, VK_PIPELINE_BIND_POINT_COMPUTE);
		vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_COMPUTE, _reflectionFilterPipeline.getLayout(), 0, 1, &_reflectionFilterDescriptorPool.getDescriptorSets()[i], 0, 0);
		const auto groupSize = 16;
		vkCmdDispatch(b, _width / groupSize, _height / groupSize, 1);

		// Generate reflection mipmaps
		// FIXME: This is not a proper filtering. At all.
		// We're merely scaling down the original image while we should resample it and filter it using an increasing roughness. This approximation still breaks down at objects
		// boundaries (or in fact any surface/normal discontinuities) but seems widely used anyway, I guess the result is convincing (If I understood correctly, ofc). An easier and
		// more accurate alternative (but obviously more expensive) would be to actualy fire multiple rays for each pixels in the previous stage. This may make this stage obsolete
		// and steer us back to using an input attachment.
		_reflectionImages[i].generateMipmaps(b, _width, _height, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_UNDEFINED);

		Image::setLayout(b, _directLightImages[i], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						 VkImageSubresourceRange{
							 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							 .baseMipLevel = 0,
							 .levelCount = 1,
							 .baseArrayLayer = 0,
							 .layerCount = 1,
						 });

		// Gather
		{
			const std::vector<VkClearValue> clearValues{
				VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
				VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.depthStencil = {1.0f, 0}},
			};
			b.beginRenderPass(_gatherRenderPass, _gatherFramebuffers[i], _swapChainExtent, clearValues);
			_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 3);
			_gatherPipeline.bind(b);
			vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _gatherPipeline.getLayout(), 0, 1, &_gatherDescriptorPool.getDescriptorSets()[i], 0, nullptr);
			vkCmdDraw(b, 3, 1, 0, 0);
			_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 4);
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

			const auto&	 m = _probeMesh.getMeshes()[0].SubMeshes[0];
			VkDeviceSize offsets[1] = {0};
			vkCmdBindVertexBuffers(b, 0, 1, &m.getVertexBuffer().getHandle(), offsets);
			vkCmdBindIndexBuffer(b, m.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(b, static_cast<uint32_t>(m.getIndices().size()),
							 _irradianceProbes.GridParameters.resolution.x * _irradianceProbes.GridParameters.resolution.y * _irradianceProbes.GridParameters.resolution.z, 0, 0,
							 0);
			b.endRenderPass();
		}

		_mainTimingQueryPools[i].writeTimestamp(b, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 5);
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

	VK_CHECK(vkDeviceWaitIdle(_device));

	cleanupSwapChain();

	createSwapChain();
	initSwapChain();
	uiOnSwapChainReady();
}

void Application::cleanupSwapChain() {
	_mainTimingQueryPools.clear();

	_rayTraceCommandBuffers.free();
	_rayTracingPipeline.destroy();
	_rayTracingDescriptorPool.destroy();
	_rayTracingDescriptorSetLayout.destroy();
	_rayTracingPipelineLayout.destroy();
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

	_cameraUniformBuffers.clear();
	_cameraUniformBuffersMemory.free();
	_lightUniformBuffers.clear();
	_lightUniformBuffersMemory.free();
	_gbufferDescriptorPool.destroy();
	_reflectionShadowDescriptorPool.destroy();
	_reflectionFilterDescriptorPool.destroy();
	_gatherDescriptorPool.destroy();
	_gbufferFramebuffers.clear();
	_gatherFramebuffers.clear();

	// Only free up the command buffer, not the command pool
	_commandBuffers.free();

	_gbufferPipeline.destroy();
	_reflectionShadowPipeline.destroy();
	_reflectionFilterPipeline.destroy();
	_gatherPipeline.destroy();

	_gbufferDescriptorSetLayouts.clear();
	_reflectionShadowDescriptorSetLayout.destroy();
	_reflectionFilterDescriptorSetLayout.destroy();
	_gatherDescriptorSetLayout.destroy();

	_gbufferRenderPass.destroy();
	_gatherRenderPass.destroy();

	_gbufferImages.clear();
	_gbufferImageViews.clear();
	_reflectionImages.clear();
	_reflectionImageViews.clear();
	_reflectionMipmapImageViews.clear();
	_reflectionFilteredImages.clear();
	_reflectionFilteredImageViews.clear();
	_directLightImages.clear();
	_directLightImageViews.clear();
	_depthImageView.destroy();
	_depthImage.destroy();
	_swapChainImageViews.clear();

	vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}
