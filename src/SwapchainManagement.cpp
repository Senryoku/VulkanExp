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
	for(size_t i = 0; i < _swapChainImages.size(); i++)
		for(size_t j = 0; j < 3; j++) {
			_gbufferImages[3 * i + j].create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
											 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
			_gbufferImages[3 * i + j].allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			_gbufferImageViews[3 * i + j].create(_device, _gbufferImages[3 * i + j], VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);
		}
	_depthFormat = findSupportedFormat(_physicalDevice, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
									   VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	_depthImage.create(_device, _swapChainExtent.width, _swapChainExtent.height, _depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	_depthImage.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_depthImageView.create(_device, _depthImage, _depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Application::initCameraBuffer() {
	VkDeviceSize bufferSize = sizeof(CameraBuffer);
	_cameraUniformBuffers.resize(_swapChainImages.size());
	for(size_t i = 0; i < _swapChainImages.size(); i++)
		_cameraUniformBuffers[i].create(_device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, bufferSize);
	auto   memReq = _cameraUniformBuffers[0].getMemoryRequirements();
	size_t memSize = (2 + _swapChainImages.size() * bufferSize / memReq.alignment) * memReq.alignment;
	_cameraUniformBuffersMemory.allocate(_device, _physicalDevice.findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
										 memSize);
	size_t offset = 0;
	_uboStride = (1 + bufferSize / memReq.alignment) * memReq.alignment;
	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		vkBindBufferMemory(_device, _cameraUniformBuffers[i], _cameraUniformBuffersMemory, offset);
		offset += _uboStride;
	}
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
	poolBuilder.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * _swapChainImages.size());
	poolBuilder.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * _swapChainImages.size());
	_probeDebugDescriptorPool = poolBuilder.build(_device, _swapChainImages.size());
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
		dsw.add(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					.sampler = *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
										   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0),
					.imageView = _irradianceProbes.getColorView(),
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				});
		dsw.add(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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
					.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
					.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			})
			.add({
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			})
			.add({
				.format = _swapChainImageFormat,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			})
			.add({
				.format = _depthFormat,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			})
			.addSubPass(VK_PIPELINE_BIND_POINT_GRAPHICS,
						{// Output
						 {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
						{// Inputs (GBuffer)
						 {0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
						 {1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
						 {2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}},
						{},
						// Depth
						{VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}, {})
			// Dependencies (FIXME!)
			.add({
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			});
		_gatherRenderPass = rpb.build(_device);

		_gatherFramebuffers.resize(_swapChainImageViews.size());
		for(size_t i = 0; i < _swapChainImageViews.size(); i++)
			_gatherFramebuffers[i].create(_device, _gatherRenderPass,
										  {
											  _gbufferImageViews[3 * i + 0],
											  _gbufferImageViews[3 * i + 1],
											  _gbufferImageViews[3 * i + 2],
											  _swapChainImageViews[i],
											  _depthImageView,
										  },
										  _swapChainExtent);
	}

	initCameraBuffer();

	createGBufferPipeline();
	createGatherPipeline();

	_commandBuffers.allocate(_device, _commandPool, _gbufferFramebuffers.size());

	_imagesInFlight.resize(_swapChainImages.size());

	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		for(size_t m = 0; m < Materials.size(); m++) {
			DescriptorSetWriter dsw(_gbufferDescriptorPool.getDescriptorSets()[i * Materials.size() + m]);
			dsw.add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					{
						.buffer = _cameraUniformBuffers[i],
						.offset = 0,
						.range = sizeof(CameraBuffer),
					});
			auto& albedo = Materials[m].albedoTexture != -1 ? Textures[Materials[m].albedoTexture] : _blankTexture;
			dsw.add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						.sampler = *albedo.sampler,
						.imageView = albedo.gpuImage->imageView,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					});
			// Use a blank texture if this mesh doesn't have a normal map
			auto& normals = Materials[m].normalTexture != -1 ? Textures[Materials[m].normalTexture] : _blankTexture;
			dsw.add(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						.sampler = *normals.sampler,
						.imageView = normals.gpuImage->imageView,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					});
			dsw.add(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						.sampler = *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
											   VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
						.imageView = _irradianceProbes.getColorView(),
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					});
			dsw.add(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						.sampler = *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
											   VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
						.imageView = _irradianceProbes.getDepthView(),
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					});
			dsw.add(5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					{
						.buffer = _irradianceProbes.getGridParametersBuffer(),
						.offset = 0,
						.range = VK_WHOLE_SIZE,
					});
			dsw.update(_device);
		}
	}

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
	// FIXME: Should not be there, just WIP
	_irradianceProbes.writeDescriptorSet(_scene, _topLevelAccelerationStructure);
	initProbeDebug();

	recordCommandBuffers();
}

void Application::recordCommandBuffers() {
	for(size_t i = 0; i < _commandBuffers.getBuffers().size(); i++) {
		auto& b = _commandBuffers.getBuffers()[i];
		b.begin();
		std::vector<VkClearValue> clearValues{
			VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
			VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}}, VkClearValue{.depthStencil = {1.0f, 0}},
		};
		b.beginRenderPass(_gbufferRenderPass, _gbufferFramebuffers[i], _swapChainExtent, clearValues);
		_gbufferPipeline.bind(b);

		const std::function<void(const glTF::Node&, glm::mat4)> visitNode = [&](const glTF::Node& n, glm::mat4 transform) {
			transform = transform * n.transform;

			if(n.mesh != -1) {
				for(const auto& submesh : _scene.getMeshes()[n.mesh].SubMeshes) {
					vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _gbufferPipeline.getLayout(), 0, 1,
											&_gbufferDescriptorPool.getDescriptorSets()[i * Materials.size() + submesh.materialIndex], 0, nullptr);
					vkCmdPushConstants(b, _gbufferPipeline.getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &transform);
					b.bind<1>({submesh.getVertexBuffer()});
					vkCmdBindIndexBuffer(b, submesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
					vkCmdDrawIndexed(b, static_cast<uint32_t>(submesh.getIndices().size()), 1, 0, 0, 0);
				}
			}

			for(const auto& c : n.children)
				visitNode(_scene.getNodes()[c], transform);
		};
		visitNode(_scene.getRoot(), glm::mat4(1.0f));

		b.endRenderPass();

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

		// TODO: Compute reflection & Shadow via Ray Tracing

		// TODO: Generate reflection mipmaps

		// Gather

		b.beginRenderPass(_gatherRenderPass, _gatherFramebuffers[i], _swapChainExtent, clearValues);
		_gatherPipeline.bind(b);
		vkCmdBindDescriptorSets(b, VK_PIPELINE_BIND_POINT_GRAPHICS, _gatherPipeline.getLayout(), 0, 1, &_gatherDescriptorPool.getDescriptorSets()[i], 0, nullptr);
		vkCmdDraw(b, 3, 1, 0, 0);
		b.endRenderPass();

		// Probes Debug
		if(_probeDebug) {
			std::vector<VkClearValue> clearValues{
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
			vkCmdDrawIndexed(b, m.getIndices().size(),
							 _irradianceProbes.GridParameters.resolution.x * _irradianceProbes.GridParameters.resolution.y * _irradianceProbes.GridParameters.resolution.z, 0, 0,
							 0);
			b.endRenderPass();
		}

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
	_rayTraceCommandBuffers.free();
	_rayTracingPipeline.destroy();
	_rayTracingDescriptorPool.destroy();
	_rayTracingDescriptorSetLayout.destroy();
	_rayTracingPipelineLayout.destroy();
	_rayTraceStorageImageView.destroy();
	_rayTraceStorageImage.destroy();

	_presentFramebuffers.clear();
	_imguiCommandBuffers.free();
	_imguiRenderPass.destroy();

	_probeDebugFramebuffers.clear();
	_probeDebugRenderPass.destroy();
	_probeDebugDescriptorPool.destroy();
	_probeDebugDescriptorSetLayouts.clear();
	_probeDebugPipeline.destroy();

	for(auto& b : _cameraUniformBuffers)
		b.destroy();
	_cameraUniformBuffersMemory.free();
	_gbufferDescriptorPool.destroy();
	_gatherDescriptorPool.destroy();
	_gbufferFramebuffers.clear();
	_gatherFramebuffers.clear();

	// Only free up the command buffer, not the command pool
	_commandBuffers.free();

	_gbufferPipeline.destroy();
	_gatherPipeline.destroy();

	_gbufferDescriptorSetLayouts.clear();
	_gatherDescriptorSetLayouts.clear();

	_gbufferRenderPass.destroy();
	_gatherRenderPass.destroy();

	_gbufferImages.clear();
	_gbufferImageViews.clear();
	_depthImageView.destroy();
	_depthImage.destroy();
	_swapChainImageViews.clear();

	vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}
