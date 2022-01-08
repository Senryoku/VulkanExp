#include "Application.hpp"

// Create final gather pipeline (No actual geometry)
void Application::createGatherPipeline() {
	Shader gatherVertShader(_device, "./shaders_spv/FullScreenQuad.vert.spv");
	Shader gatherFragShader(_device, "./shaders_spv/FinalGather.frag.spv");

	std::vector<VkPipelineShaderStageCreateInfo> gatherShaderStages{
		gatherVertShader.getStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
		gatherFragShader.getStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	DescriptorSetLayoutBuilder builder;
	builder.add(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT)
		.add(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT)
		.add(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT)
		.add(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT)			 // Direct Light
		.add(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // Reflection
		.add(6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)		 // Grid Parameters
		.add(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)		 // Probe Info
		.add(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // Probes Color
		.add(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) // Probes Depth
		.add(10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);		 // Camera
	_gatherDescriptorSetLayout = builder.build(_device);

	uint32_t			  descriptorSetsCount = static_cast<uint32_t>(_swapChainImages.size());
	DescriptorPoolBuilder poolBuilder;
	poolBuilder.add(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 3 * descriptorSetsCount);
	_gatherDescriptorPool = poolBuilder.build(_device, descriptorSetsCount);

	std::vector<VkDescriptorSetLayout> descriptorSetsLayoutsToAllocate;
	for(size_t i = 0; i < _swapChainImages.size(); ++i)
		descriptorSetsLayoutsToAllocate.push_back(_gatherDescriptorSetLayout);
	_gatherDescriptorPool.allocate(descriptorSetsLayoutsToAllocate);

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 0,
		.pVertexBindingDescriptions = nullptr,
		.vertexAttributeDescriptionCount = 0,
		.pVertexAttributeDescriptions = nullptr,
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
		.cullMode = VK_CULL_MODE_FRONT_BIT,
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

	for(size_t i = 0; i < _swapChainImages.size(); ++i) {
		DescriptorSetWriter dsw(_gatherDescriptorPool.getDescriptorSets()[i]);
		dsw.add(0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				{
					.sampler =
						*getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
					.imageView = _gbufferImageViews[3 * i + 0],
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				})
			.add(1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				 {
					 .sampler =
						 *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
					 .imageView = _gbufferImageViews[3 * i + 1],
					 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				 })
			.add(2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				 {
					 .sampler =
						 *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
					 .imageView = _gbufferImageViews[3 * i + 2],
					 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				 })
			.add(3, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				 {
					 .sampler =
						 *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
					 .imageView = _directLightImageViews[i],
					 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				 })
			.add(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				 {
					 .sampler = *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
											VK_SAMPLER_ADDRESS_MODE_REPEAT, static_cast<float>(_reflectionImages[i].getMipLevels())),
					 .imageView = _reflectionMipmapImageViews[i],
					 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				 })
			.add(6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				 {
					 .buffer = _irradianceProbes.getGridParametersBuffer(),
					 .offset = 0,
					 .range = sizeof(IrradianceProbes::GridInfo),
				 })
			.add(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				 {
					 .buffer = _irradianceProbes.getProbeInfoBuffer(),
					 .offset = 0,
					 .range = VK_WHOLE_SIZE,
				 })
			.add(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				 {
					 .sampler =
						 *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
					 .imageView = _irradianceProbes.getColorView(),
					 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				 })
			.add(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				 {
					 .sampler =
						 *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0),
					 .imageView = _irradianceProbes.getDepthView(),
					 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				 })
			.add(10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				 {
					 .buffer = _cameraUniformBuffers[i],
					 .offset = 0,
					 .range = sizeof(CameraBuffer),
				 });
		dsw.update(_device);
	}

	_gatherPipeline.getLayout().create(_device, {_gatherDescriptorSetLayout});

	VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<uint32_t>(gatherShaderStages.size()),
		.pStages = gatherShaderStages.data(),
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssembly,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pDepthStencilState = &depthStencil, // Optional
		.pColorBlendState = &colorBlending,
		.pDynamicState = nullptr, // Optional
		.layout = _gatherPipeline.getLayout(),
		.renderPass = _gatherRenderPass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE, // Optional
		.basePipelineIndex = -1,			  // Optional
	};

	_gatherPipeline.create(_device, pipelineInfo, _pipelineCache);
}
