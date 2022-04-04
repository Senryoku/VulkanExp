#include "Editor.hpp"

void Editor::createGBufferPass() {
	createGBufferRenderPass();
	createGBufferFramebuffers();
	createGBufferPipeline();
}

void Editor::createGBufferRenderPass() {
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
					 {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
					 {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
					 {4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
					{}, {},
					{// Depth
					 5, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
					{})
		// Dependencies (FIXME!)
		.add({
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		})
		.add({
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		});
	_gbufferRenderPass = rpb.build(_device);
}

void Editor::createGBufferFramebuffers() {
	_gbufferFramebuffers.resize(_swapChainImageViews.size());
	for(size_t i = 0; i < _swapChainImageViews.size(); i++)
		_gbufferFramebuffers[i].create(_device, _gbufferRenderPass,
									   {
										   _gbufferImageViews[_gbufferSize * i + 0],
										   _gbufferImageViews[_gbufferSize * i + 1],
										   _gbufferImageViews[_gbufferSize * i + 2],
										   _gbufferImageViews[_gbufferSize * i + 3],
										   _gbufferImageViews[_gbufferSize * i + 4],
										   _depthImageView,
									   },
									   _swapChainExtent);
}

void Editor::createGBufferPipeline() {
	assert(_gbufferRenderPass.isValid());
	for(size_t i = 0; i < _swapChainImageViews.size(); i++)
		assert(_gbufferFramebuffers[i].isValid());

	Shader vertShader(_device, "./shaders_spv/GBuffer.vert.spv");
	Shader fragShader(_device, "./shaders_spv/GBuffer.frag.spv");

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
		vertShader.getStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
		fragShader.getStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	DescriptorSetLayoutBuilder builder;
	builder
		.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) // Camera
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)					   // Albedo
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)					   // Normal
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)					   // Metal Roughness
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)					   // Emissive
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT);							   // Material
	_gbufferDescriptorSetLayouts.push_back(builder.build(_device));

	DescriptorSetLayoutBuilder instanceSetBuilder;
	instanceSetBuilder
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)	 // Instance transform SSBO
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT); // Previous instance transform SSBO
	_gbufferDescriptorSetLayouts.push_back(instanceSetBuilder.build(_device));

	std::vector<VkDescriptorSetLayout> layouts;
	for(const auto& layout : _gbufferDescriptorSetLayouts)
		layouts.push_back(layout);

	uint32_t			  materialDescriptorSetsCount = static_cast<uint32_t>(_swapChainImages.size() * Materials.size());
	uint32_t			  instanceDescriptorSetsCount = static_cast<uint32_t>(_swapChainImages.size());
	DescriptorPoolBuilder poolBuilder;
	poolBuilder.add(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * materialDescriptorSetsCount)
		.add(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * materialDescriptorSetsCount)
		.add(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, materialDescriptorSetsCount + instanceDescriptorSetsCount);
	_gbufferDescriptorPool = poolBuilder.build(_device, materialDescriptorSetsCount + instanceDescriptorSetsCount);

	std::vector<VkDescriptorSetLayout> descriptorSetsLayoutsToAllocate;
	for(size_t i = 0; i < _swapChainImages.size(); ++i)
		for(const auto& material : Materials) {
			descriptorSetsLayoutsToAllocate.push_back(_gbufferDescriptorSetLayouts[0]);
		}
	for(size_t i = 0; i < _swapChainImages.size(); ++i)
		descriptorSetsLayoutsToAllocate.push_back(_gbufferDescriptorSetLayouts[1]);
	_gbufferDescriptorPool.allocate(descriptorSetsLayoutsToAllocate);

	auto										   bindingDescription = {Vertex::getBindingDescription()};
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions{Vertex::getAttributeDescriptions().begin(), Vertex::getAttributeDescriptions().end()};

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescription.size()),
		.pVertexBindingDescriptions = bindingDescription.begin(),
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
		.pVertexAttributeDescriptions = attributeDescriptions.data(),
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
	std::array<VkPipelineColorBlendAttachmentState, 5> colorBlendAttachmentStates{
		colorBlendAttachment, colorBlendAttachment, colorBlendAttachment, colorBlendAttachment, colorBlendAttachment,
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
		.attachmentCount = static_cast<uint32_t>(colorBlendAttachmentStates.size()),
		.pAttachments = colorBlendAttachmentStates.data(),
		.blendConstants = {0.0f, 0.0f, 0.0f, 0.0f} // Optional
	};

	VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_LINE_WIDTH};

	VkPipelineDynamicStateCreateInfo dynamicState{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamicStates,
	};

	_gbufferPipeline.getLayout().create(_device, layouts);

	VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = static_cast<uint32_t>(shaderStages.size()),
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssembly,
		.pViewportState = &viewportState,
		.pRasterizationState = &rasterizer,
		.pMultisampleState = &multisampling,
		.pDepthStencilState = &depthStencil, // Optional
		.pColorBlendState = &colorBlending,
		.pDynamicState = nullptr, // Optional
		.layout = _gbufferPipeline.getLayout(),
		.renderPass = _gbufferRenderPass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE, // Optional
		.basePipelineIndex = -1,			  // Optional
	};

	_gbufferPipeline.create(_device, pipelineInfo, _pipelineCache);

	Shader skinnedVertShader(_device, "./shaders_spv/GBufferSkinned.vert.spv");

	{
		std::vector<VkPipelineShaderStageCreateInfo> skinnedShaderStages{
			skinnedVertShader.getStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
			fragShader.getStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT),
		};
		auto bindingDescription = {
			Vertex::getBindingDescription(),
			VkVertexInputBindingDescription{
				.binding = 1,
				.stride = sizeof(glm::vec4),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
			},
		};
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{Vertex::getAttributeDescriptions().begin(), Vertex::getAttributeDescriptions().end()};
		attributeDescriptions.push_back(VkVertexInputAttributeDescription{
			.location = 5,
			.binding = 1,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = 0,
		});

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescription.size()),
			.pVertexBindingDescriptions = bindingDescription.begin(),
			.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
			.pVertexAttributeDescriptions = attributeDescriptions.data(),
		};

		pipelineInfo.pStages = skinnedShaderStages.data();
		pipelineInfo.pVertexInputState = &vertexInputInfo;

		_gbufferSkinnedPipeline.create(_device, pipelineInfo, _pipelineCache);
	}
	writeGBufferDescriptorSets();
}

void Editor::writeGBufferDescriptorSets() {
	// Write descriptor sets for each material, for each image in the swap chain.
	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		for(size_t m = 0; m < Materials.size(); m++) {
			DescriptorSetWriter dsw(_gbufferDescriptorPool.getDescriptorSets()[i * Materials.size() + m]);
			dsw.add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					{
						.buffer = _cameraUniformBuffers[i],
						.offset = 0,
						.range = sizeof(CameraBuffer),
					});
			auto& albedo = Materials[m].properties.albedoTexture != -1 ? Textures[Materials[m].properties.albedoTexture] : *_blankTexture;
			dsw.add(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						.sampler = *albedo.sampler,
						.imageView = albedo.gpuImage->imageView,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					});
			// Use a blank texture if this mesh doesn't have a normal map
			auto& normals = Materials[m].properties.normalTexture != -1 ? Textures[Materials[m].properties.normalTexture] : *_blankTexture;
			dsw.add(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						.sampler = *normals.sampler,
						.imageView = normals.gpuImage->imageView,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					});
			auto& metallicRoughness = Materials[m].properties.metallicRoughnessTexture != -1 ? Textures[Materials[m].properties.metallicRoughnessTexture] : *_blankTexture;
			dsw.add(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						.sampler = *metallicRoughness.sampler,
						.imageView = metallicRoughness.gpuImage->imageView,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					});
			auto& emissive = Materials[m].properties.emissiveTexture != -1 ? Textures[Materials[m].properties.emissiveTexture] : *_blankTexture;
			dsw.add(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{
						.sampler = *emissive.sampler,
						.imageView = emissive.gpuImage->imageView,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					});
			dsw.add(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					{
						.buffer = MaterialBuffer,
						.offset = m * sizeof(Material::Properties),
						.range = sizeof(Material::Properties),
					});
			dsw.update(_device);
		}
	}
	for(size_t i = 0; i < _swapChainImages.size(); i++) {
		DescriptorSetWriter dsw(_gbufferDescriptorPool.getDescriptorSets()[_swapChainImages.size() * Materials.size() + i]);
		dsw.add(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _renderer.getInstanceBuffer());
		dsw.add(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, _renderer.getPreviousInstanceBuffer());
		dsw.update(_device);
	}
}

void Editor::destroyGBufferPipeline() {
	_gbufferPipeline.destroy();
	_gbufferSkinnedPipeline.destroy();
	_gbufferDescriptorPool.destroy();
	_gbufferDescriptorSetLayouts.clear();
}
