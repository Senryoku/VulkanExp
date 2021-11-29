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
	auto				 commandBuffer = _commandBuffers.getBuffers()[imageIndex].getHandle();

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
						.maxVertex = 3,
						.indexType = VK_INDEX_TYPE_UINT32,
						.indexData = _scene.getMeshes()[0].getIndexBuffer().getDeviceAddress(),
						.transformData = 0,
					},
			},
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
	};

	VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &acceleration_structure_geometry,
	};

	const uint32_t primitiveCount = 1;

	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	vkGetAccelerationStructureBuildSizesKHR(_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &acceleration_structure_build_geometry_info, &primitiveCount,
											&accelerationStructureBuildSizesInfo);

	_arBuffer.create(_device, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, accelerationStructureBuildSizesInfo.accelerationStructureSize);

	// Create the acceleration structure
	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = _arBuffer,
		.size = accelerationStructureBuildSizesInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
	};
	vkCreateAccelerationStructureKHR(_device, &accelerationStructureCreateInfo, nullptr, &_bottomLevelAccelerationStructure);

	// Temporary buffer used for Acceleration Creation
	Buffer		 scratchBuffer;
	DeviceMemory scratchMemory;
	scratchBuffer.create(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, accelerationStructureBuildSizesInfo.buildScratchSize);
	scratchMemory.allocate(_device, scratchBuffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR);

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.pNext = VK_NULL_HANDLE,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.srcAccelerationStructure = VK_NULL_HANDLE,
		.dstAccelerationStructure = _bottomLevelAccelerationStructure,
		.geometryCount = 1,
		.pGeometries = &acceleration_structure_geometry,
		.ppGeometries = nullptr,
		.scratchData = {.deviceAddress = scratchBuffer.getDeviceAddress()},
	};

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{
		.primitiveCount = 1,
		.primitiveOffset = 0,
		.firstVertex = 0,
		.transformOffset = 0,
	};
	std::array<VkAccelerationStructureBuildRangeInfoKHR*, 1> accelerationStructureBuildRangeInfos = {&accelerationStructureBuildRangeInfo};

	// Build the acceleration structure on the device via a one-time command buffer submission
	// Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we
	// prefer device builds
	immediateSubmit([&](const CommandBuffer& commandBuffer) {
		vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationBuildGeometryInfo, accelerationStructureBuildRangeInfos.data());
	});

	// Get the bottom acceleration structure's handle, which will be used during the top level acceleration build
	VkAccelerationStructureDeviceAddressInfoKHR acceleration_device_address_info{
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = _bottomLevelAccelerationStructure,
	};
	auto BLASDeviceAddress = vkGetAccelerationStructureDeviceAddressKHR(_device, &acceleration_device_address_info);
}
