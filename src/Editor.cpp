#include "Editor.hpp"

#include <ImGuizmo.h>
#include <Raytracing.hpp>
#include <voxels/Chunk.hpp>
#include <vulkan/Extension.hpp>

void Editor::initWindow() {
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
	glfwSetWindowUserPointer(_window, this); // Allow access to our Editor instance in callbacks
	glfwSetFramebufferSizeCallback(_window, sFramebufferResizeCallback);
	glfwSetMouseButtonCallback(_window, sMouseButtonCallback);
	glfwSetScrollCallback(_window, sScrollCallback);
	glfwSetDropCallback(_window, sDropCallback);
	glfwSetKeyCallback(_window, sKeyCallback);

	_shortcuts[{GLFW_KEY_F1}] = [&]() { _drawUI = !_drawUI; };
	_shortcuts[{GLFW_KEY_S, GLFW_PRESS, GLFW_MOD_CONTROL}] = [&]() { _scene.save("data/defaut.scene"); };
	_shortcuts[{GLFW_KEY_D, GLFW_PRESS, GLFW_MOD_CONTROL}] = [&]() { duplicateSelectedNode(); };
	_shortcuts[{GLFW_KEY_X, GLFW_PRESS}] = [&]() { _useSnap = !_useSnap; };
	_shortcuts[{GLFW_KEY_T, GLFW_PRESS}] = [&]() { _currentGizmoOperation = ImGuizmo::TRANSLATE; };
	_shortcuts[{GLFW_KEY_Z, GLFW_PRESS}] = [&]() { _currentGizmoOperation = ImGuizmo::TRANSLATE; };
	_shortcuts[{GLFW_KEY_R, GLFW_PRESS}] = [&]() { _currentGizmoOperation = ImGuizmo::ROTATE; };
	_shortcuts[{GLFW_KEY_Y, GLFW_PRESS}] = [&]() { _currentGizmoOperation = ImGuizmo::SCALE; };
	_shortcuts[{GLFW_KEY_DELETE, GLFW_PRESS}] = [&]() { deleteSelectedNode(); };
}

void Editor::deleteSelectedNode() {
	_scene.getRegistry().destroy(_selectedNode);
	_selectedNode = entt::null;

	_dirtyHierarchy = true;
}

void Editor::duplicateSelectedNode() {
	if(_selectedNode == entt::null)
		return;

	std::function<entt::entity(entt::entity)> copyNode = [&](entt::entity src) {
		auto&	   registry = _scene.getRegistry();
		const auto dst = registry.create();
		// Opaque copy of all components
		for(auto&& curr : registry.storage()) {
			if(auto& storage = curr.second; storage.contains(src)) {
				storage.emplace(dst, storage.get(src));
			}
		}
		auto& srcNode = registry.get<NodeComponent>(src);
		auto& dstNode = registry.get<NodeComponent>(dst);
		dstNode.children = 0;
		dstNode.parent = entt::null;
		dstNode.first = entt::null;
		dstNode.next = entt::null;
		dstNode.prev = entt::null;
		for(auto c = srcNode.first; c != entt::null; c = registry.get<NodeComponent>(c).next)
			_scene.addChild(dst, copyNode(c));
		return dst;
	};

	auto copiedNode = copyNode(_selectedNode);
	_scene.addSibling(_selectedNode, copiedNode);
	_selectedNode = copiedNode;

	_dirtyHierarchy = true;
}

void Editor::run() {
	// Make sure shaders are up-to-date
	system("powershell.exe -ExecutionPolicy RemoteSigned .\\compile_shaders.ps1");

	// Default Material
	Materials.push_back(Material{.name = "Default Material"});

	{
		QuickTimer qt("Scene loading");

		/*
		// Chunk test
		Chunk chunk;
		float h = Chunk::Size / 2.0;
		for(int i = 0; i < Chunk::Size; ++i)
			for(int j = 0; j < Chunk::Size; ++j)
				for(int k = 0; k < Chunk::Size; ++k) {
					chunk(i, j, k).type = (i - h) * (i - h) + (j - h) * (j - h) + (k - h) * (k - h) < h * h ? 1 : Voxel::Empty;
				}
		_scene.loadMaterial("data/materials/cavern-deposits/cavern-deposits.mat");
		_scene.getMeshes().emplace_back(generateMesh(chunk));
		_scene.getMeshes().back().computeBounds();
		_scene.getMeshes().back().defaultMaterialIndex = MaterialIndex(Materials.size() - 1);

		auto  entity = _scene.getRegistry().create();
		auto& node = _scene.getRegistry().emplace<NodeComponent>(entity);
		node.name = "Chunk";
		_scene.addChild(_scene.getRoot(), entity);
		auto& renderer = _scene.getRegistry().emplace<MeshRendererComponent>(entity);
		renderer.meshIndex = static_cast<MeshIndex>(_scene.getMeshes().size() - 1);
		renderer.materialIndex = _scene.getMeshes().back().defaultMaterialIndex;
		*/

		for(const auto& str : {
				//"./data/models/Sponza/Sponza.gltf",
				//"./data/models/MetalRoughSpheres/MetalRoughSpheres.gltf",
				//"./data/models/lucy.obj",
				//"./data/models/Helmet/DamagedHelmet.gltf",
				//"./data/models/sanmiguel/sanmiguel.gltf",
				//"./data/models/livingroom/livingroom.gltf",
				//"./data/models/gallery/gallery.gltf", // Crashes
				//"./data/models/Home/ConvertedWithBlendergltf.gltf",
				//"./data/models/Home/untitled.gltf",
				"./data/models/MetalRoughSpheres/MetalRoughSpheres.gltf",
				//"./data/models/SunTemple-glTF/suntemple.gltf",
				//"./data/models/postwar_city_-_exterior_scene/scene.gltf",
				//"./data/models/sea_keep_lonely_watcher/scene.gltf",
				"./data/models/RiggedSimple/glTF/RiggedSimple.gltf",
			})
			_scene.load(str);
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

void Editor::mainLoop() {
	std::chrono::time_point lastUpdate = std::chrono::high_resolution_clock::now();

	while(!glfwWindowShouldClose(_window)) {
		glfwPollEvents();

		if(_dirtyShaders) {
			compileShaders();
			_dirtyShaders = false;
		}

		if(_dirtyHierarchy) {
			// Recreate Acceleration Structure
			vkDeviceWaitIdle(_device); // TODO: Better sync?
			_renderer.destroyTLAS();
			_renderer.createTLAS();
			onTLASCreation();
			_outdatedCommandBuffers = true;
			_dirtyHierarchy = false;
		}

		for(auto& pool : _mainTimingQueryPools) {
			if(pool.newSampleFlag) {
				auto results = pool.get();
				if(results.size() >= 6 && results[0].available && results[5].available) {
					_frameTimes.add(0.000001f * (results[5].result - results[0].result));
					_gbufferTimes.add(0.000001f * (results[2].result - results[1].result));
					_reflectionDirectLightTimes.add(0.000001f * (results[3].result - results[2].result));
					_reflectionDirectLightFilterTimes.add(0.000001f * (results[4].result - results[3].result));
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
		if(_drawUI)
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

		const auto						   time = std::chrono::high_resolution_clock::now();
		const std::chrono::duration<float> delta = _timeScale * (time - lastUpdate);
		const auto						   deltaTime = delta.count();
		lastUpdate = time;

		const auto updates = _scene.update(deltaTime);
		if(updates)
			_renderer.onHierarchicalChanges(deltaTime);
		if(_outdatedCommandBuffers || updates) {
			std::vector<VkFence> fencesHandles;
			fencesHandles.reserve(_inFlightFences.size());
			for(const auto& fence : _inFlightFences)
				fencesHandles.push_back(fence);
			VK_CHECK(vkWaitForFences(_device, static_cast<uint32_t>(fencesHandles.size()), fencesHandles.data(), VK_TRUE, UINT64_MAX));
			recordCommandBuffers();
			recordRayTracingCommands();
			_outdatedCommandBuffers = false;
		}

		drawFrame();
	}
}

void Editor::drawFrame() {
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
		if(_enableReflections)
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

	uint32_t commandBufferCount = 2;
	if(!_raytracingDebug)
		++commandBufferCount;

	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = waitSemaphores,
		.pWaitDstStageMask = waitStages,
		.commandBufferCount = commandBufferCount,
		.pCommandBuffers = cmdbuff,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = signalSemaphores,
	};
	VK_CHECK(vkResetFences(_device, 1, &currentFence));
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submitInfo, currentFence));
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

void Editor::cameraControl(float dt) {
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

void Editor::trySelectNode() {
	auto ratio = static_cast<float>(_height) / _width;
	auto d = glm::normalize(static_cast<float>((2.0f * _mouse_x) / _width - 1.0f) * _camera.getRight() +
							-ratio * static_cast<float>((2.0f * _mouse_y) / _height - 1.0f) * glm::cross(_camera.getRight(), _camera.getDirection()) + _camera.getDirection());
	Ray	 r{.origin = _camera.getPosition(), .direction = d};
	if(auto node = _scene.intersectNodes(r); node != entt::null) {
		_selectedNode = node;
	}
}

#include <glm/gtx/euler_angles.hpp>

double toRad(double degree) {
	return (degree * (3.14159265359 / 180));
}

void Editor::updateUniformBuffer(uint32_t currentImage) {
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
			// (Original paper: https://www.sciencedirect.com/science/article/pii/S0960148121004031?via%3Dihub)
			double latssp = declination;																  // latitude of the subsolar point
			double equationOfTime = 0;																	  // in minutes (omitted, but mentioned for completion)
			double lonssp = toRad(-15.0 * (_hour + _minute / 60.0 - _utctimezone + equationOfTime / 60)); // longitude of the subsolar point
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

void Editor::onTLASCreation() {
	// FIXME: Also abstract this somehow? (Callback from createTLAS? setup by Scene?)
	// We have to update the all descriptor sets referencing the acceleration structures.
	for(auto set : {&_directLightDescriptorPool, &_reflectionDescriptorPool, &_rayTracingDescriptorPool})
		for(size_t i = 0; i < _swapChainImages.size(); ++i) {
			DescriptorSetWriter dsw(set->getDescriptorSets()[i]);
			dsw.add(0, {
						   .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
						   .accelerationStructureCount = 1,
						   .pAccelerationStructures = &_renderer.getTLAS(),
					   });
			dsw.update(_device);
		}
	_irradianceProbes.writeDescriptorSet(_renderer, _lightUniformBuffers[0]);
	// GBuffer also uses the transform buffer that was just re-created
	writeGBufferDescriptorSets();
}

void Editor::sFramebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto app = reinterpret_cast<Editor*>(glfwGetWindowUserPointer(window));
	app->_framebufferResized = true;
	app->_width = width;
	app->_height = height;
}

void Editor::sScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
	if(ImGui::GetIO().WantCaptureMouse)
		return;
	auto app = reinterpret_cast<Editor*>(glfwGetWindowUserPointer(window));
	if(yoffset > 0)
		app->_camera.speed *= 1.1f;
	else
		app->_camera.speed *= (1.f / 1.1f);
};

void Editor::sMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if(ImGui::GetIO().WantCaptureMouse)
		return;
	auto app = reinterpret_cast<Editor*>(glfwGetWindowUserPointer(window));
	if(button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		glfwGetCursorPos(window, &app->_mouse_x, &app->_mouse_y);
		app->trySelectNode();
	} else if(button == GLFW_MOUSE_BUTTON_RIGHT) {
		app->_controlCamera = action == GLFW_PRESS;
		glfwGetCursorPos(window, &app->_mouse_x, &app->_mouse_y);
		glfwSetInputMode(window, GLFW_CURSOR, action == GLFW_PRESS ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	}
}

void Editor::sDropCallback(GLFWwindow* window, int pathCount, const char* paths[]) {
	auto app = reinterpret_cast<Editor*>(glfwGetWindowUserPointer(window));
	vkDeviceWaitIdle(app->_device); // FIXME: Do better?
	for(int i = 0; i < pathCount; ++i) {
		print("Received path '{}'.\n", paths[i]);
		app->_scene.load(paths[i]);
	}
	// FIXME: This is way overkill
	app->uploadScene();
	// Since the number of material may have changed, we have to re-create GBuffer descriptor layout and sets
	app->destroyGBufferPipeline();
	app->destroyDirectLightPipeline();
	app->destroyReflectionPipeline();
	app->destroyRayTracingPipeline();
	app->createGBufferPipeline();
	app->createDirectLightPass();
	app->createReflectionPass();
	app->createRayTracingPipeline();
	app->createRaytracingDescriptorSets();
	app->recordRayTracingCommands();
	app->_irradianceProbes.destroyPipeline();
	app->_irradianceProbes.createPipeline();
	app->onTLASCreation();
	app->uiOnTextureChange();
	app->_outdatedCommandBuffers = true;
}

void Editor::sKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	auto	 app = reinterpret_cast<Editor*>(glfwGetWindowUserPointer(window));
	ImGuiIO& io = ImGui::GetIO();
	if(io.WantCaptureMouse || app->_controlCamera)
		return;
	auto it = app->_shortcuts.find({key, action, mods});
	if(it != app->_shortcuts.end())
		it->second();
}
