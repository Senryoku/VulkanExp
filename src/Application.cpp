#include "Application.hpp"

#include <ImGuizmo.h>
#include <Raytracing.hpp>
#include <RaytracingDescriptors.hpp>
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
	glfwSetMouseButtonCallback(_window, sMouseButtonCallback);
	glfwSetScrollCallback(_window, sScrollCallback);
	glfwSetDropCallback(_window, sDropCallback);
}

void Application::run() {
	// Make sure shaders are up-to-date
	system("powershell.exe -ExecutionPolicy RemoteSigned .\\compile_shaders.ps1");

	// Default Material
	Materials.push_back(Material{.name = "Default Material"});

	{
		QuickTimer qt("Scene loading");
		_scene.load("./data/models/Sponza/Sponza.gltf");
		_scene.load("./data/models/MetalRoughSpheres/MetalRoughSpheres.gltf");
		//_scene.load("./data/models/lucy.obj");
		//_scene.load("./data/models/Helmet/DamagedHelmet.gltf");
		//_scene.loadglTF("./data/models/sanmiguel/sanmiguel.gltf");
		//_scene.loadglTF("./data/models/livingroom/livingroom.gltf");
		//_scene.loadglTF("./data/models/gallery/gallery.gltf"); // Crashes
		//_scene.loadglTF("./data/models/Home/ConvertedWithBlendergltf.gltf");
		//_scene.loadglTF("./data/models/Home/untitled.gltf");
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
					_gbufferTimes.add(0.000001f * (results[2].result - results[1].result));
					_reflectionDirectLightTimes.add(0.000001f * (results[3].result - results[2].result));
					_reflectionFilterTimes.add(0.000001f * (results[4].result - results[3].result));
					_gatherTimes.add(0.000001f * (results[5].result - results[4].result));
					pool.newSampleFlag = false;
					static auto lastPresentTime = results[5].result;
					if(results[5].result < lastPresentTime) {
						// Queries are out of order? (Or frames?!) Not sure what's going on :(
						_presentTimes.add(0.000001f * (lastPresentTime - results[5].result));
					} else {
						_presentTimes.add(0.000001f * (results[5].result - lastPresentTime));
						lastPresentTime = results[5].result;
					}
				}
			}
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
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

		const auto updates = _scene.update(_device);
		if(_outdatedCommandBuffers || updates) {
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

	// Record copy operations (previous reflection & previous direct light)
	if(!_framebufferResized) { // FIXME: This is a workaround; When the window is resized _width and _height are not valid anymore until recreateSwapChain is called and recreating
							   // the copy command buffer with wrong values will crash the application.
		auto& copyCmdBuff = _copyCommandBuffers.getBuffers()[imageIndex];
		copyCmdBuff.begin();
		VkImageCopy copy{
			.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.srcOffset = {0, 0, 0},
			.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
			.dstOffset = {0, 0, 0},
			.extent = {_width, _height, 1},
		};
		const auto copyImage = [&](const Image& srcImage, const Image& dstImage) {
			dstImage.barrier(copyCmdBuff, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
							 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			srcImage.barrier(copyCmdBuff, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
							 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			vkCmdCopyImage(copyCmdBuff, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
			srcImage.barrier(copyCmdBuff, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
							 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
			dstImage.barrier(copyCmdBuff, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
							 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
		};
		copyImage(_reflectionImages[_lastImageIndex], _reflectionImages[imageIndex + _swapChainImages.size()]);
		copyImage(_directLightImages[_lastImageIndex], _directLightImages[imageIndex + _swapChainImages.size()]);
		_copyCommandBuffers.getBuffers()[imageIndex].end();
	}

	// Submit all command buffers at once
	VkCommandBuffer cmdbuff[3]{
		_copyCommandBuffers.getBuffers()[imageIndex].getHandle(),
		commandBuffer,
		_imguiCommandBuffers.getBuffers()[imageIndex].getHandle(),
	};

	if(_raytracingDebug) { // Skip reflection copies for raytracing debug display
		cmdbuff[0] = commandBuffer;
		cmdbuff[1] = _imguiCommandBuffers.getBuffers()[imageIndex].getHandle();
	}

	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = waitSemaphores,
		.pWaitDstStageMask = waitStages,
		.commandBufferCount = _raytracingDebug ? 2u : 3u,
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

	_lastImageIndex = imageIndex;
	_currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	++_frameIndex;
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

void Application::trySelectNode() {
	auto ratio = static_cast<float>(_height) / _width;
	auto d = glm::normalize(static_cast<float>((2.0f * _mouse_x) / _width - 1.0f) * _camera.getRight() +
							-ratio * static_cast<float>((2.0f * _mouse_y) / _height - 1.0f) * glm::cross(_camera.getRight(), _camera.getDirection()) + _camera.getDirection());
	Ray	 r{.origin = _camera.getPosition(), .direction = d};
	auto node = _scene.intersectNodes(r);
	if(node != nullptr) {
		_selectedNode = node;
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

	static CameraBuffer previousUBO;
	{
		// Copy current to previous
		{
			void*  data;
			size_t offset = static_cast<size_t>(_swapChainImages.size() + currentImage) * _uboStride;
			VK_CHECK(vkMapMemory(_device, _cameraUniformBuffersMemory, offset, sizeof(previousUBO), 0, &data));
			memcpy(data, &previousUBO, sizeof(previousUBO));
			vkUnmapMemory(_device, _cameraUniformBuffersMemory);
		}

		cameraControl(time);

		_camera.updateView();
		_camera.updateProjection(_swapChainExtent.width / (float)_swapChainExtent.height);

		CameraBuffer ubo{
			.view = _camera.getViewMatrix(),
			.proj = _camera.getProjectionMatrix(),
			.frameIndex = _frameIndex,
		};
		ubo.proj[1][1] *= -1;

		void*  data;
		size_t offset = static_cast<size_t>(currentImage) * _uboStride;
		VK_CHECK(vkMapMemory(_device, _cameraUniformBuffersMemory, offset, sizeof(ubo), 0, &data));
		memcpy(data, &ubo, sizeof(ubo));
		vkUnmapMemory(_device, _cameraUniformBuffersMemory);

		previousUBO = ubo;
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
