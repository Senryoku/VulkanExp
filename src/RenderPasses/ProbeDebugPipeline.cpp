#include <Editor.hpp>

void Editor::createProbeDebugPass() {
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
		dsw.add(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _irradianceProbes.getProbeInfoBuffer());
		dsw.add(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					.sampler = *getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
										   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0),
					.imageView = _irradianceProbes.getIrradianceView(),
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
