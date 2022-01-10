#include "Application.hpp"

#include <vulkan/Extension.hpp>

void Application::initWindow() {
	if(glfwInit() != GLFW_TRUE)
		error("Error intialising GLFW.\n");

	glfwWindowHint(GLFW_CLIENT_API,
				   GLFW_NO_API); // Opt-out of creating an OpenGL Context
	// glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, 1);
	// glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	// glfwWindowHint(GLFW_DECORATED, false);

	_window = glfwCreateWindow(InitialWidth, InitialHeight, "VulkanExp", nullptr, nullptr);
	if(_window == nullptr)
		error("Error while creating GLFW Window. ");

	// Setup GLFW Callbacks
	glfwSetWindowUserPointer(_window, this); // Allow access to our Application instance in callbacks
	glfwSetFramebufferSizeCallback(_window, framebufferResizeCallback);
	glfwSetMouseButtonCallback(_window, mouse_button_callback);
	glfwSetScrollCallback(_window, scroll_callback);
}

void Application::run() {
	// Make sure shaders are up-to-date
	system("powershell.exe -ExecutionPolicy RemoteSigned .\\compile_shaders.ps1");
	{
		QuickTimer qt("glTF load");
		//_scene.loadglTF("./data/models/MetalRoughSpheres/MetalRoughSpheres.gltf");
		_scene.loadglTF("./data/models/Sponza/Sponza.gltf");
		//_scene.loadglTF("./data/models/MetalRoughSpheres/MetalRoughSpheres.gltf", Scene::LoadOperation::AppendToCurrentScene);
		//_scene.loadglTF("./data/models/SunTemple-glTF/suntemple.gltf");
		//_scene.loadglTF("./data/models/postwar_city_-_exterior_scene/scene.gltf");
		//_scene.loadglTF("./data/models/sea_keep_lonely_watcher/scene.gltf");
	}
	_probeMesh.loadglTF("./data/models/sphere.gltf");
	{
		QuickTimer qt("initWindow");
		initWindow();
	}
	{
		QuickTimer qt("initVulkan");
		initVulkan();
	}

	mainLoop();

	// Waits for the GPU to be done before cleaning everything up
	VK_CHECK(vkDeviceWaitIdle(_device));

	cleanup();
}

void Application::mainLoop() {
	while(!glfwWindowShouldClose(_window)) {
		glfwPollEvents();

		if(_dirtyShaders) {
			compileShaders();
			_dirtyShaders = false;
		}

		for(auto& pool : _mainTimingQueryPools) {
			if(pool.newSampleFlag) {
				auto results = pool.get();
				if(results.size() >= 6 && results[0].available && results[5].available) {
					_frameTimes.add(0.000001f * (results[5].result - results[0].result));
					pool.newSampleFlag = false;
				}
			}
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
		drawUI();
		ImGui::Render();
		// Update and Render additional Platform Windows
		if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		if(_irradianceProbeAutoUpdate) {
			_irradianceProbes.setLightBuffer(_lightUniformBuffers[_lastImageIndex]);
			_irradianceProbes.update(_scene, _computeQueue);
		}

		if(_outdatedCommandBuffers) {
			std::vector<VkFence> fencesHandles;
			fencesHandles.reserve(_inFlightFences.size());
			for(const auto& fence : _inFlightFences)
				fencesHandles.push_back(fence);
			VK_CHECK(vkWaitForFences(_device, static_cast<uint32_t>(fencesHandles.size()), fencesHandles.data(), VK_TRUE, UINT64_MAX));
			recordCommandBuffers();
			_outdatedCommandBuffers = false;
		}

		drawFrame();
	}
}

void Application::drawFrame() {
	VkFence currentFence = _inFlightFences[_currentFrame];
	VK_CHECK(vkWaitForFences(_device, 1, &currentFence, VK_TRUE, UINT64_MAX));

	uint32_t imageIndex;
	auto	 result = vkAcquireNextImageKHR(_device, _swapChain, UINT64_MAX, _imageAvailableSemaphore[_currentFrame], VK_NULL_HANDLE, &imageIndex);
	_lastImageIndex = imageIndex;

	if(result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreateSwapChain();
		return;
	} else if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error(fmt::format("Failed to acquire swap chain image. (Error: {})", result));
	}

	// Check if a previous frame is using this image (i.e. there is its
	// fence to wait on)
	if(_imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
		VK_CHECK(vkWaitForFences(_device, 1, &_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX));
	}
	// Mark the image as now being in use by this frame
	_imagesInFlight[imageIndex] = currentFence;

	updateUniformBuffer(imageIndex);

	VkSemaphore			 waitSemaphores[] = {_imageAvailableSemaphore[_currentFrame]};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphore			 signalSemaphores[] = {_renderFinishedSemaphore[_currentFrame]}; // Synchronize render and presentation

	auto commandBuffer = _raytracingDebug ? _rayTraceCommandBuffers.getBuffers()[imageIndex].getHandle() : _commandBuffers.getBuffers()[imageIndex].getHandle();
	recordUICommandBuffer(imageIndex);

	// Submit both command buffers
	VkCommandBuffer cmdbuff[2]{commandBuffer, _imguiCommandBuffers.getBuffers()[imageIndex].getHandle()};
	VkSubmitInfo	submitInfo{
		   .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		   .waitSemaphoreCount = 1,
		   .pWaitSemaphores = waitSemaphores,
		   .pWaitDstStageMask = waitStages,
		   .commandBufferCount = 2,
		   .pCommandBuffers = cmdbuff,
		   .signalSemaphoreCount = 1,
		   .pSignalSemaphores = signalSemaphores,
	   };
	VK_CHECK(vkResetFences(_device, 1, &currentFence));
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, currentFence));
	if(!_raytracingDebug)
		_mainTimingQueryPools[imageIndex].newSampleFlag = true;

	// Present the new frame
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
		throw std::runtime_error(fmt::format("Failed to present swap chain image. (Error: {})", toString(result)));
	}

	_currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Application::cameraControl(float dt) {
	static glm::vec3 cameraPosition{0.0f};
	if(_controlCamera) {
		if(glfwGetKey(_window, GLFW_KEY_W) == GLFW_PRESS)
			_camera.moveForward(dt);

		if(glfwGetKey(_window, GLFW_KEY_A) == GLFW_PRESS)
			_camera.strafeLeft(dt);

		if(glfwGetKey(_window, GLFW_KEY_S) == GLFW_PRESS)
			_camera.moveBackward(dt);

		if(glfwGetKey(_window, GLFW_KEY_D) == GLFW_PRESS)
			_camera.strafeRight(dt);

		if(glfwGetKey(_window, GLFW_KEY_Q) == GLFW_PRESS)
			_camera.moveDown(dt);

		if(glfwGetKey(_window, GLFW_KEY_E) == GLFW_PRESS || glfwGetKey(_window, GLFW_KEY_SPACE) == GLFW_PRESS)
			_camera.moveUp(dt);

		double mx = _mouse_x, my = _mouse_y;
		glfwGetCursorPos(_window, &_mouse_x, &_mouse_y);
		if(_mouse_x != mx || _mouse_y != my)
			_camera.look(glm::vec2(_mouse_x - mx, my - _mouse_y));
	}
}

#include <glm/gtx/euler_angles.hpp>

double toRad(double degree) {
	return (degree * (3.14159265359 / 180));
}

void Application::updateUniformBuffer(uint32_t currentImage) {
	static auto lastTime = std::chrono::high_resolution_clock::now();
	auto		currentTime = std::chrono::high_resolution_clock::now();
	float		time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
	lastTime = currentTime;

	{
		cameraControl(time);

		_camera.updateView();
		_camera.updateProjection(_swapChainExtent.width / (float)_swapChainExtent.height);

		CameraBuffer ubo{
			.view = _camera.getViewMatrix(),
			.proj = _camera.getProjectionMatrix(),
		};
		ubo.proj[1][1] *= -1;

		void*  data;
		size_t offset = static_cast<size_t>(currentImage) * _uboStride; // FIXME: 256 is the alignment (> sizeof(ubo)), should be correctly saved somewhere
		VK_CHECK(vkMapMemory(_device, _cameraUniformBuffersMemory, offset, sizeof(ubo), 0, &data));
		memcpy(data, &ubo, sizeof(ubo));
		vkUnmapMemory(_device, _cameraUniformBuffersMemory);
	}

	{
		if(_deriveLightPositionFromTime) {
			_minute += _dayCycleSpeed * time / 60.0f;
			while(_minute > 60) {
				_hour = ++_hour;
				_minute -= 60;
			}
			while(_hour > 24) {
				_dayOfTheYear = (_dayOfTheYear + 1) % 365;
				_hour -= 24;
			}

			double fractionalYear = 2 * 3.14159265359 / 365.0 * (_dayOfTheYear - 1.0 + (_hour - 12.0) / 24.0);
			double declination = 0.006918 - 0.399912 * std::cos(fractionalYear) + 0.070257 * std::sin(fractionalYear) - 0.006758 * std::cos(2 * fractionalYear) +
								 0.000907 * std::sin(2 * fractionalYear) - 0.002697 * std::cos(3 * fractionalYear) + 0.00148 * std::sin(3 * fractionalYear);
			double lat = toRad(_latitude);
			double lon = toRad(_longitude);
			// See https://en.wikipedia.org/wiki/Solar_azimuth_angle#The_formula_based_on_the_subsolar_point_and_the_atan2_function (with some simplifations)
			double latssp = declination; // latitude of the subsolar point
			// FIXME: Time scale in completely wrong, but I assume T(GMT) is in hours in the original paper
			// (https://www.sciencedirect.com/science/article/pii/S0960148121004031?via%3Dihub) so I'm not sure what's wrong
			double lonssp = -15.0 * (_hour + _minute / 60.0 - _utctimezone - 12.0 + 0 / 60); // longitude of the subsolar point
			_light.direction = glm::vec4{
				std::cos(latssp) * std::sin(lonssp - lon),
				std::cos(lat) * std::sin(latssp) - std::sin(lat) * std::cos(latssp) * std::cos(lonssp - lon),
				std::sin(lat) * std::sin(latssp) + std::cos(lat) * std::cos(latssp) * std::cos(lonssp - lon),
				1.0,
			};
		}
		size_t offset = static_cast<size_t>(currentImage) * _lightUboStride;
		_lightUniformBuffersMemory.fill(reinterpret_cast<char*>(&_light), sizeof(LightBuffer), offset);
	}
}
