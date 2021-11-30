#include "Application.hpp"

#include <vulkan/Extension.hpp>

void Application::initWindow() {
	fmt::print("Window initialisation... ");

	if(!glfwInit())
		error("Error intialising GLFW.\n");

	glfwWindowHint(GLFW_CLIENT_API,
				   GLFW_NO_API); // Opt-out of creating an OpenGL Context
	// glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, 1);
	// glfwWindowHint(GLFW_DECORATED, false);

	_window = glfwCreateWindow(InitialWidth, InitialHeight, "VulkanExp", nullptr, nullptr);
	if(_window == nullptr)
		error("Error while creating GLFW Window. ");

	// Setup GLFW Callbacks
	glfwSetWindowUserPointer(_window, this); // Allow access to our Application instance in callbacks
	glfwSetFramebufferSizeCallback(_window, framebufferResizeCallback);
	glfwSetMouseButtonCallback(_window, mouse_button_callback);
	glfwSetScrollCallback(_window, scroll_callback);

	success("Done.\n");
}

void Application::drawFrame() {
	VkFence currentFence = _inFlightFences[_currentFrame];
	vkWaitForFences(_device, 1, &currentFence, VK_TRUE, UINT64_MAX);

	uint32_t imageIndex;
	auto	 result = vkAcquireNextImageKHR(_device, _swapChain, UINT64_MAX, _imageAvailableSemaphore[_currentFrame], VK_NULL_HANDLE, &imageIndex);

	if(result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapChain();
		return;
	} else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error(fmt::format("Failed to acquire swap chain image. (Error: {})", result));
	}

	// Check if a previous frame is using this image (i.e. there is its
	// fence to wait on)
	if(_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
		vkWaitForFences(_device, 1, &_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
	}
	// Mark the image as now being in use by this frame
	_imagesInFlight[imageIndex] = currentFence;

	updateUniformBuffer(imageIndex);

	VkSemaphore			 waitSemaphores[] = {_imageAvailableSemaphore[_currentFrame]};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphore			 signalSemaphores[] = {_renderFinishedSemaphore[_currentFrame]};
	auto				 commandBuffer = _rayTraceCommandBuffers.getBuffers()[imageIndex].getHandle();
	// auto				 commandBuffer = _commandBuffers.getBuffers()[imageIndex].getHandle();

	// Dear IMGUI
	auto imguiCmdBuff = _imguiCommandBuffers.getBuffers()[imageIndex].getHandle();
	if(vkResetCommandPool(_device, _imguiCommandPool, 0) != VK_SUCCESS)
		throw std::runtime_error("vkResetCommandPool error");
	VkCommandBufferBeginInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	if(vkBeginCommandBuffer(imguiCmdBuff, &info) != VK_SUCCESS)
		throw std::runtime_error("vkBeginCommandBuffer error");
	std::array<VkClearValue, 1> clearValues{
		VkClearValue{.color = {0.0f, 0.0f, 0.0f, 1.0f}},
	};
	VkRenderPassBeginInfo rpinfo{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = _imguiRenderPass,
		.framebuffer = _imguiFramebuffers[imageIndex],
		.renderArea = {.extent = _swapChainExtent},
		.clearValueCount = 1,
		.pClearValues = clearValues.data(),
	};
	vkCmdBeginRenderPass(imguiCmdBuff, &rpinfo, VK_SUBPASS_CONTENTS_INLINE);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), imguiCmdBuff);
	vkCmdEndRenderPass(imguiCmdBuff);
	vkEndCommandBuffer(imguiCmdBuff);

	VkCommandBuffer cmdbuff[2]{commandBuffer, imguiCmdBuff};

	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = waitSemaphores,
		.pWaitDstStageMask = waitStages,
		.commandBufferCount = 2,
		.pCommandBuffers = cmdbuff,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = signalSemaphores,
	};

	vkResetFences(_device, 1, &currentFence);
	if(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, currentFence) != VK_SUCCESS) {
		throw std::runtime_error("Failed to submit draw command buffer!");
	}

	VkSwapchainKHR	 swapChains[] = {_swapChain};
	VkPresentInfoKHR presentInfo{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = signalSemaphores,
		.swapchainCount = 1,
		.pSwapchains = swapChains,
		.pImageIndices = &imageIndex,
		.pResults = nullptr // Optional
	};
	result = vkQueuePresentKHR(_presentQueue, &presentInfo);

	if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _framebufferResized) {
		_framebufferResized = false;
		recreateSwapChain();
	} else if(result != VK_SUCCESS) {
		throw std::runtime_error(fmt::format("Failed to present swap chain image. (Error: {})", result));
	}

	_currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Application::cameraControl(float dt) {
	static glm::vec3 cameraPosition{0.0f};
	if(_controlCamera) {
		if(glfwGetKey(_window, GLFW_KEY_W) == GLFW_PRESS) {
			_camera.moveForward(dt);
		}

		if(glfwGetKey(_window, GLFW_KEY_A) == GLFW_PRESS) {
			_camera.strafeLeft(dt);
		}

		if(glfwGetKey(_window, GLFW_KEY_S) == GLFW_PRESS) {
			_camera.moveBackward(dt);
		}

		if(glfwGetKey(_window, GLFW_KEY_D) == GLFW_PRESS) {
			_camera.strafeRight(dt);
		}

		if(glfwGetKey(_window, GLFW_KEY_Q) == GLFW_PRESS) {
			_camera.moveDown(dt);
		}

		if(glfwGetKey(_window, GLFW_KEY_E) == GLFW_PRESS) {
			_camera.moveUp(dt);
		}

		double mx = _mouse_x, my = _mouse_y;
		glfwGetCursorPos(_window, &_mouse_x, &_mouse_y);
		if(_mouse_x != mx || _mouse_y != my)
			_camera.look(glm::vec2(_mouse_x - mx, my - _mouse_y));
	}
}

#include <glm/gtx/euler_angles.hpp>

void Application::updateUniformBuffer(uint32_t currentImage) {
	static auto lastTime = std::chrono::high_resolution_clock::now();
	auto		currentTime = std::chrono::high_resolution_clock::now();
	float		time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
	lastTime = currentTime;

	cameraControl(time);

	UniformBufferObject ubo{};

	_camera.updateView();
	_camera.updateProjection(_swapChainExtent.width / (float)_swapChainExtent.height);

	ubo.model = glm::mat4(1.0f);
	ubo.view = _camera.getViewMatrix();
	ubo.proj = _camera.getProjectionMatrix(); // glm::perspective(glm::radians(45.0f), _swapChainExtent.width / (float)_swapChainExtent.height, _camera.nearPlane, _farPlane);
	ubo.proj[1][1] *= -1;

	void*  data;
	size_t offset = static_cast<size_t>(currentImage) * 256; // FIXME: 256 is the alignment (> sizeof(ubo)), should be correctly saved somewhere
	vkMapMemory(_device, _uniformBuffersMemory, offset, sizeof(ubo), 0, &data);
	memcpy(data, &ubo, sizeof(ubo));
	vkUnmapMemory(_device, _uniformBuffersMemory);
}

// Raytracing

void Application::createStorageImage() {
	_rayTraceStorageImage.create(_device, _swapChainExtent.width, _swapChainExtent.height, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
								 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	_rayTraceStorageImage.allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	_rayTraceStorageImageView.create(_device, VkImageViewCreateInfo{
												  .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
												  .image = _rayTraceStorageImage,
												  .viewType = VK_IMAGE_VIEW_TYPE_2D,
												  .format = VK_FORMAT_B8G8R8A8_UNORM,
												  .subresourceRange =
													  VkImageSubresourceRange{
														  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
														  .baseMipLevel = 0,
														  .levelCount = 1,
														  .baseArrayLayer = 0,
														  .layerCount = 1,
													  },
											  });

	_rayTraceStorageImage.transitionLayout(_physicalDevice.getQueues(_surface).graphicsFamily.value(), VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
										   VK_IMAGE_LAYOUT_GENERAL);
}

void Application::createAccelerationStructure() {
	VkFormatProperties2 formatProperties{
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};
	vkGetPhysicalDeviceFormatProperties2(_physicalDevice, VK_FORMAT_R32G32B32_SFLOAT, &formatProperties);
	assert(formatProperties.formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR);

	VkTransformMatrixKHR transformMatrix = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

	Buffer		 transformBuffer;
	DeviceMemory transformMemory;
	transformBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, sizeof(VkTransformMatrixKHR));
	transformMemory.allocate(_device, transformBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	transformMemory.fill(&transformMatrix, 1);

	VkAccelerationStructureGeometryKHR acceleration_structure_geometry{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry =
			VkAccelerationStructureGeometryDataKHR{
				.triangles =
					VkAccelerationStructureGeometryTrianglesDataKHR{
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexData = _scene.getMeshes()[0].getVertexBuffer().getDeviceAddress(),
						.vertexStride = sizeof(Vertex),
						.maxVertex = static_cast<uint32_t>(_scene.getMeshes()[0].getVertices().size()),
						.indexType = VK_INDEX_TYPE_UINT16,
						.indexData = _scene.getMeshes()[0].getIndexBuffer().getDeviceAddress(),
						.transformData = transformBuffer.getDeviceAddress(),
					},
			},
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
	};

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.pNext = VK_NULL_HANDLE,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &acceleration_structure_geometry,
		.ppGeometries = nullptr,
	};

	const uint32_t primitiveCount = static_cast<uint32_t>(_scene.getMeshes()[0].getIndices().size() / 3);

	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationBuildGeometryInfo, &primitiveCount,
											&accelerationStructureBuildSizesInfo);

	_blasBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, accelerationStructureBuildSizesInfo.accelerationStructureSize);
	_blasMemory.allocate(_device, _blasBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Create the acceleration structure
	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.pNext = VK_NULL_HANDLE,
		.buffer = _blasBuffer,
		.size = accelerationStructureBuildSizesInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
	};
	VK_CHECK(vkCreateAccelerationStructureKHR(_device, &accelerationStructureCreateInfo, nullptr, &_bottomLevelAccelerationStructure));

	// Temporary buffer used for Acceleration Creation
	Buffer		 scratchBuffer;
	DeviceMemory scratchMemory;
	scratchBuffer.create(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, accelerationStructureBuildSizesInfo.buildScratchSize);
	scratchMemory.allocate(_device, scratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);

	// Complete the build infos.
	accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	accelerationBuildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
	accelerationBuildGeometryInfo.dstAccelerationStructure = _bottomLevelAccelerationStructure;
	accelerationBuildGeometryInfo.scratchData = {.deviceAddress = scratchBuffer.getDeviceAddress()};

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{
		.primitiveCount = primitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> accelerationStructureBuildRangeInfos = {&accelerationStructureBuildRangeInfo};

	// Build the acceleration structure on the device via a one-time command buffer submission
	// Some implementations may support acceleration structure building on the host (vkBuildAccelerationStructuresKHR), but we
	// prefer device builds
	immediateSubmit([&](const CommandBuffer& commandBuffer) {
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationBuildGeometryInfo, accelerationStructureBuildRangeInfos.data());
	});

	// Get the bottom acceleration structure's handle, which will be used during the top level acceleration build
	VkAccelerationStructureDeviceAddressInfoKHR BLASAddressInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = _bottomLevelAccelerationStructure,
	};
	auto BLASDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(_device, &BLASAddressInfo);

	VkAccelerationStructureInstanceKHR accelerationStructureInstance{
		.transform = transformMatrix,
		.instanceCustomIndex = 0,
		.mask = 0xFF,
		.instanceShaderBindingTableRecordOffset = 0,
		.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
		.accelerationStructureReference = BLASDeviceAddress,
	};

	Buffer		 instanceBuffer;
	DeviceMemory instanceMemory;
	instanceBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
						  sizeof(VkAccelerationStructureInstanceKHR));
	instanceMemory.allocate(_device, instanceBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	instanceMemory.fill(&accelerationStructureInstance, 1);

	VkAccelerationStructureGeometryKHR TLASGeometry{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.geometry =
			{
				.instances =
					{
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
						.arrayOfPointers = VK_FALSE,
						.data = instanceBuffer.getDeviceAddress(),
					},
			},
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
	};

	VkAccelerationStructureBuildGeometryInfoKHR TLASBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &TLASGeometry,
	};

	const uint32_t							 TBLAPrimitiveCount = 1;
	VkAccelerationStructureBuildSizesInfoKHR TLASBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &TLASBuildGeometryInfo, &TBLAPrimitiveCount, &TLASBuildSizesInfo);

	_tlasBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, TLASBuildSizesInfo.accelerationStructureSize);
	_tlasMemory.allocate(_device, _tlasBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkAccelerationStructureCreateInfoKHR TLASCreateInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = _tlasBuffer,
		.size = TLASBuildSizesInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
	};

	VK_CHECK(vkCreateAccelerationStructureKHR(_device, &TLASCreateInfo, nullptr, &_topLevelAccelerationStructure));

	scratchBuffer.destroy();
	scratchMemory.free();
	scratchBuffer.create(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, TLASBuildSizesInfo.buildScratchSize);
	scratchMemory.allocate(_device, scratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);

	TLASBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	TLASBuildGeometryInfo.dstAccelerationStructure = _topLevelAccelerationStructure;
	TLASBuildGeometryInfo.scratchData = {.deviceAddress = scratchBuffer.getDeviceAddress()};

	VkAccelerationStructureBuildRangeInfoKHR TLASBuildRangeInfo{
		.primitiveCount = TBLAPrimitiveCount,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> TLASBuildRangeInfos = {&TLASBuildRangeInfo};

	immediateSubmit([&](const CommandBuffer& commandBuffer) { vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &TLASBuildGeometryInfo, TLASBuildRangeInfos.data()); });
}

void Application::createRayTracingPipeline() {

	// Slot for binding top level acceleration structures to the ray generation shader
	VkDescriptorSetLayoutBinding acceleration_structure_layout_binding{
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};

	VkDescriptorSetLayoutBinding result_image_layout_binding{
		.binding = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};

	VkDescriptorSetLayoutBinding uniform_buffer_binding{
		.binding = 2,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};
	std::array<VkDescriptorSetLayoutBinding, 3> bindings = {acceleration_structure_layout_binding, result_image_layout_binding, uniform_buffer_binding};

	VkDescriptorSetLayoutCreateInfo layout_info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = static_cast<uint32_t>(bindings.size()),
		.pBindings = bindings.data(),
	};
	_rayTracingDescriptorSetLayout.create(_device, layout_info);

	VkPipelineLayoutCreateInfo pipeline_layout_create_info{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &_rayTracingDescriptorSetLayout.getHandle(),
	};

	_rayTracingPipelineLayout.create(_device, pipeline_layout_create_info);

	/*
		Setup ray tracing shader groups
		Each shader group points at the corresponding shader in the pipeline
	*/
	std::vector<VkPipelineShaderStageCreateInfo>	  shader_stages;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

	Shader raygenShader(_device, "./shaders_spv/raygen.rgen.spv");
	shader_stages.push_back(raygenShader.getStageCreateInfo(VK_SHADER_STAGE_RAYGEN_BIT_KHR));
	Shader raymissShader(_device, "./shaders_spv/miss.rmiss.spv");
	shader_stages.push_back(raymissShader.getStageCreateInfo(VK_SHADER_STAGE_MISS_BIT_KHR));
	Shader closesthitShader(_device, "./shaders_spv/closesthit.rchit.spv");
	shader_stages.push_back(closesthitShader.getStageCreateInfo(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
	// Ray generation group
	{
		VkRayTracingShaderGroupCreateInfoKHR raygen_group_ci{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 0,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
		shader_groups.push_back(raygen_group_ci);
	}

	// Ray miss group
	{
		VkRayTracingShaderGroupCreateInfoKHR miss_group_ci{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 1,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
		shader_groups.push_back(miss_group_ci);
	}

	// Ray closest hit group
	{
		VkRayTracingShaderGroupCreateInfoKHR closes_hit_group_ci{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = 2,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
		shader_groups.push_back(closes_hit_group_ci);
	}

	VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_create_info{
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = static_cast<uint32_t>(shader_stages.size()),
		.pStages = shader_stages.data(),
		.groupCount = static_cast<uint32_t>(shader_groups.size()),
		.pGroups = shader_groups.data(),
		.maxPipelineRayRecursionDepth = 1,
		.layout = _rayTracingPipelineLayout,
	};
	_rayTracingPipeline.create(_device, raytracing_pipeline_create_info);
}

void Application::createRaytracingDescriptorSets() {
	_rayTracingDescriptorPool.create(_device, 1,
									 std::array<VkDescriptorPoolSize, 3>{
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
										 VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
									 });
	_rayTracingDescriptorPool.allocate(1, _rayTracingDescriptorSetLayout);

	// Setup the descriptor for binding our top level acceleration structure to the ray tracing shaders
	VkWriteDescriptorSetAccelerationStructureKHR descriptor_acceleration_structure_info{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &_topLevelAccelerationStructure,
	};

	VkWriteDescriptorSet acceleration_structure_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = &descriptor_acceleration_structure_info, // The acceleration structure descriptor has to be chained via pNext
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
	};

	VkDescriptorImageInfo image_descriptor{
		.imageView = _rayTraceStorageImageView,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkDescriptorBufferInfo buffer_descriptor{
		.buffer = _uniformBuffers[0],
		.offset = 0,
		.range = sizeof(UniformBufferObject),
	};

	VkWriteDescriptorSet result_image_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 1,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.pImageInfo = &image_descriptor,
	};
	VkWriteDescriptorSet uniform_buffer_write{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = _rayTracingDescriptorPool.getDescriptorSets()[0],
		.dstBinding = 2,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_descriptor,
	};

	std::vector<VkWriteDescriptorSet> write_descriptor_sets = {acceleration_structure_write, result_image_write, uniform_buffer_write};
	vkUpdateDescriptorSets(_device, static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, VK_NULL_HANDLE);
}

inline uint32_t aligned_size(uint32_t value, uint32_t alignment) {
	return (value + alignment - 1) & ~(alignment - 1);
}

void Application::recordRayTracingCommands() {
	VkPhysicalDeviceRayTracingPropertiesNV rayTracingPipelineProperties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
	VkPhysicalDeviceProperties2			   deviceProperties{
				   .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
				   .pNext = &rayTracingPipelineProperties,
	   };
	vkGetPhysicalDeviceProperties2(_device.getPhysicalDevice(), &deviceProperties);
	const uint32_t		 handle_size_aligned = aligned_size(rayTracingPipelineProperties.shaderGroupHandleSize, rayTracingPipelineProperties.shaderGroupBaseAlignment);
	auto				 stb_size = 3 * handle_size_aligned;
	std::vector<uint8_t> shader_handle_storage(stb_size);
	VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(_device, _rayTracingPipeline, 0, 3, stb_size, shader_handle_storage.data()));

	for(size_t i = 0; i < 3; ++i) {
		_rayTracingShaderBindingTables[i].create(_device,
												 VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
												 rayTracingPipelineProperties.shaderGroupHandleSize);
		_rayTracingShaderBindingTablesMemory[i].allocate(_device, _rayTracingShaderBindingTables[i],
														 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		_rayTracingShaderBindingTablesMemory[i].fill(shader_handle_storage.data() + i * handle_size_aligned, rayTracingPipelineProperties.shaderGroupHandleSize);
	}

	VkImageSubresourceRange subresource_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	for(size_t i = 0; i < _rayTraceCommandBuffers.getBuffers().size(); i++) {
		auto cmdBuff = _rayTraceCommandBuffers.getBuffers()[i];
		cmdBuff.begin();

		/*
			Setup the strided device address regions pointing at the shader identifiers in the shader binding table
		*/

		const uint32_t handle_size_aligned = aligned_size(rayTracingPipelineProperties.shaderGroupHandleSize, rayTracingPipelineProperties.shaderGroupBaseAlignment);

		VkStridedDeviceAddressRegionKHR raygen_shader_sbt_entry{};
		raygen_shader_sbt_entry.deviceAddress = _rayTracingShaderBindingTables[0].getDeviceAddress();
		raygen_shader_sbt_entry.stride = handle_size_aligned;
		raygen_shader_sbt_entry.size = handle_size_aligned;

		VkStridedDeviceAddressRegionKHR miss_shader_sbt_entry{};
		miss_shader_sbt_entry.deviceAddress = _rayTracingShaderBindingTables[1].getDeviceAddress();
		miss_shader_sbt_entry.stride = handle_size_aligned;
		miss_shader_sbt_entry.size = handle_size_aligned;

		VkStridedDeviceAddressRegionKHR hit_shader_sbt_entry{};
		hit_shader_sbt_entry.deviceAddress = _rayTracingShaderBindingTables[2].getDeviceAddress();
		hit_shader_sbt_entry.stride = handle_size_aligned;
		hit_shader_sbt_entry.size = handle_size_aligned;

		VkStridedDeviceAddressRegionKHR callable_shader_sbt_entry{};

		/*
			Dispatch the ray tracing commands
		*/
		vkCmdBindPipeline(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _rayTracingPipeline);
		vkCmdBindDescriptorSets(cmdBuff, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, _rayTracingPipelineLayout, 0, 1, &_rayTracingDescriptorPool.getDescriptorSets()[0], 0, 0);

		vkCmdTraceRaysKHR(cmdBuff, &raygen_shader_sbt_entry, &miss_shader_sbt_entry, &hit_shader_sbt_entry, &callable_shader_sbt_entry, _width, _height, 1);

		/*
			Copy ray tracing output to swap chain image
		*/

		// Prepare current swap chain image as transfer destination
		Image::setLayout(cmdBuff, _swapChainImages[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresource_range);
		// Prepare ray tracing output image as transfer source
		Image::setLayout(cmdBuff, _rayTraceStorageImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresource_range);

		VkImageCopy copy_region{
			.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.srcOffset = {0, 0, 0},
			.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.dstOffset = {0, 0, 0},
			.extent = {_width, _height, 1},
		};
		vkCmdCopyImage(cmdBuff, _rayTraceStorageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _swapChainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

		// Transition swap chain image back for presentation
		Image::setLayout(cmdBuff, _swapChainImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						 subresource_range); // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR probably no correct: we still have to render dear imgui
		// Transition ray tracing output image back to general layout
		Image::setLayout(cmdBuff, _rayTraceStorageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, subresource_range);

		/*
			Start a new render pass to draw the UI overlay on top of the ray traced image
		*/
		/*
		VkClearValue clear_values[2];
		clear_values[0].color = {{0.0f, 0.0f, 0.2f, 0.0f}};
		clear_values[1].depthStencil = {0.0f, 0};

		VkRenderPassBeginInfo render_pass_begin_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = _renderPass,
			.framebuffer = _swapChainFramebuffers[i],
			.renderArea =
				{
					.extent =
						{
							.width = _width,
							.height = _height,
						},
				},
			.clearValueCount = 2,
			.pClearValues = clear_values,
		};

		vkCmdBeginRenderPass(cmdBuff, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
		draw_ui(draw_cmd_buffers[i]);
		vkCmdEndRenderPass(cmdBuff);
		*/

		VK_CHECK(vkEndCommandBuffer(cmdBuff));
	}
}
