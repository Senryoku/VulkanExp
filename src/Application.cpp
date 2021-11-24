#include "Application.hpp"

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

#include <glm/gtx/euler_angles.hpp>

void Application::updateUniformBuffer(uint32_t currentImage) {
	static auto startTime = std::chrono::high_resolution_clock::now();
	auto		currentTime = std::chrono::high_resolution_clock::now();
	float		time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	UniformBufferObject ubo{};

	if(_moving) {
		double xpos, ypos;
		glfwGetCursorPos(_window, &xpos, &ypos);
		float dx = 2.f * 3.14159f * static_cast<float>(_last_xpos - xpos) / _swapChainExtent.width, dy = 3.14159f * static_cast<float>(_last_ypos - ypos) / _swapChainExtent.height;
		static float x = 0, y = 0;
		x -= dx;
		y += dy;
		_last_xpos = xpos;
		_last_ypos = ypos;
		ubo.model = glm::eulerAngleYXZ(x, y, 0.0f);
		ubo.view = glm::lookAt(_cameraZoom * glm::normalize(glm::vec3(-1.0f, 1.0f, 0)), _cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
	} else {
		ubo.model = glm::rotate(glm::mat4(1.0f), 0.1f * time * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		ubo.view = glm::lookAt(_cameraZoom * glm::normalize(glm::vec3(-1.0f, 1.0f, 0)), _cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
	}

	ubo.proj = glm::perspective(glm::radians(45.0f), _swapChainExtent.width / (float)_swapChainExtent.height, _nearPlane, _farPlane);
	ubo.proj[1][1] *= -1;

	void*  data;
	size_t offset = static_cast<size_t>(currentImage) * 256; // FIXME: 256 is the alignment (> sizeof(ubo)), should be correctly saved somewhere
	vkMapMemory(_device, _uniformBuffersMemory, offset, sizeof(ubo), 0, &data);
	memcpy(data, &ubo, sizeof(ubo));
	vkUnmapMemory(_device, _uniformBuffersMemory);
}
