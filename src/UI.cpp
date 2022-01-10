#include "Application.hpp"

#include <ImGuiExtensions.hpp>
#include <implot/implot.h>

struct TextureRef {
	const Texture& const texture;
	const ImTextureID	 imID;
};

static std::vector<TextureRef> SceneUITextureIDs;
static ImTextureID			   ProbesColor;
static ImTextureID			   ProbesDepth;

struct DebugTexture {
	const std::string name;
	const ImTextureID id;
};
static std::vector<DebugTexture> DebugTextureIDs;

static Scene::Node* SelectedNode = nullptr;

void Application::initImGui(uint32_t queueFamily) {
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;	// Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
	// io.ConfigViewportsNoAutoMerge = true;
	// io.ConfigViewportsNoTaskBarIcon = true;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.6f;
	// ImGui::GetStyle().Colors[ImGuiCol_ChildBg].w = 0.2f;
	//  ImGui::StyleColorsClassic();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		style.WindowRounding = 0.0f;
		// style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Create Dear ImGUI Descriptor Pool
	VkDescriptorPoolSize	   pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
										   {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
										   {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
										   {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
										   {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
										   {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
										   {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
										   {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
										   {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
										   {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
										   {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes),
		.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(pool_sizes)),
		.pPoolSizes = pool_sizes,
	};
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &_imguiDescriptorPool));

	ImGui_ImplGlfw_InitForVulkan(_window, true);
	ImGui_ImplVulkan_InitInfo init_info = {
		.Instance = _instance,
		.PhysicalDevice = _physicalDevice,
		.Device = _device,
		.QueueFamily = queueFamily,
		.Queue = _graphicsQueue,
		.PipelineCache = _pipelineCache,
		.DescriptorPool = _imguiDescriptorPool,
		.MinImageCount = 2,
		.ImageCount = static_cast<uint32_t>(_swapChainImages.size()),
		.Allocator = VK_NULL_HANDLE,
		.CheckVkResultFn = nullptr,
	};
	ImGui_ImplVulkan_Init(&init_info, _imguiRenderPass);

	immediateSubmitGraphics([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	for(const auto& texture : Textures) {
		SceneUITextureIDs.push_back({texture, ImGui_ImplVulkan_AddTexture(texture.sampler->getHandle(), texture.gpuImage->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	}
	ProbesColor = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getColorView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	ProbesDepth = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getDepthView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Application::createImGuiRenderPass() {
	// UI
	VkAttachmentReference colorAttachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	_imguiRenderPass.create(_device,
							std::array<VkAttachmentDescription, 1>{VkAttachmentDescription{
								.format = _swapChainImageFormat,
								.samples = VK_SAMPLE_COUNT_1_BIT,
								.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
								.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
								.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
								.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
								.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
								.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
							}},
							std::array<VkSubpassDescription, 1>{VkSubpassDescription{
								.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
								.colorAttachmentCount = 1,
								.pColorAttachments = &colorAttachment,
							}},
							std::array<VkSubpassDependency, 1>{VkSubpassDependency{
								.srcSubpass = VK_SUBPASS_EXTERNAL,
								.dstSubpass = 0,
								.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
								.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
								.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
								.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
							}});
	_imguiCommandBuffers.allocate(_device, _imguiCommandPool, _swapChainImageViews.size());
}

void Application::uiOnSwapChainReady() {
	DebugTextureIDs.clear();
	for(size_t i = 0; i < _reflectionImageViews.size(); ++i)
		DebugTextureIDs.push_back({fmt::format("Reflection {}", i), ImGui_ImplVulkan_AddTexture(Samplers[0], _reflectionImageViews[i], VK_IMAGE_LAYOUT_GENERAL)});
	for(size_t i = 0; i < _directLightImageViews.size(); ++i)
		DebugTextureIDs.push_back(
			{fmt::format("Direct Light {}", i), ImGui_ImplVulkan_AddTexture(Samplers[0], _directLightImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	for(size_t i = 0; i < _gbufferImageViews.size(); ++i)
		DebugTextureIDs.push_back({fmt::format("GBuffer {}", i), ImGui_ImplVulkan_AddTexture(Samplers[0], _gbufferImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	for(size_t i = 0; i < _reflectionImageViews.size(); ++i)
		DebugTextureIDs.push_back(
			{fmt::format("Reflection Filtered {}", i), ImGui_ImplVulkan_AddTexture(Samplers[0], _reflectionFilteredImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
}

template<class T>
void plot(const RollingBuffer<T>& rb) {
	auto data = rb.get();
	ImPlot::PlotLine("Frame Time (ms)", data.first, static_cast<int>(data.firstCount));
	ImPlot::PlotLine("Frame Time (ms)", data.second, static_cast<int>(data.secondCount), 1.0, data.firstCount);
}

// Re-record Dear IMGUI command buffer for this frame.
void Application::recordUICommandBuffer(size_t imageIndex) {
	auto					 imguiCmdBuff = _imguiCommandBuffers.getBuffers()[imageIndex].getHandle();
	VkCommandBufferBeginInfo info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	VK_CHECK(vkBeginCommandBuffer(imguiCmdBuff, &info));
	std::array<VkClearValue, 1> clearValues{
		VkClearValue{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
	};
	VkRenderPassBeginInfo rpinfo{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = _imguiRenderPass,
		.framebuffer = _presentFramebuffers[imageIndex],
		.renderArea = {.extent = _swapChainExtent},
		.clearValueCount = 1,
		.pClearValues = clearValues.data(),
	};
	vkCmdBeginRenderPass(imguiCmdBuff, &rpinfo, VK_SUBPASS_CONTENTS_INLINE);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), imguiCmdBuff);
	vkCmdEndRenderPass(imguiCmdBuff);
	VK_CHECK(vkEndCommandBuffer(imguiCmdBuff));
}

void Application::drawUI() {
	size_t	   treeUniqueIdx = 0;
	const auto makeUnique = [&](const std::string& name) { return (name + "##" + std::to_string(++treeUniqueIdx)); };

	if(ImGui::BeginMainMenuBar()) {
		if(ImGui::BeginMenu("File")) {
			if(ImGui::MenuItem("Load Scene")) {}
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Debug")) {
			if(ImGui::MenuItem("Compile Shaders")) {
				compileShaders();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if(ImGui::Begin("Probes Debug", nullptr, ImGuiWindowFlags_HorizontalScrollbar)) {
		if(ImGui::Checkbox("Probe Debug Display", &_probeDebug)) {
			_outdatedCommandBuffers = true;
		}
		if(ImGui::Button("Rebuild probe pipeline")) {
			_irradianceProbes.createPipeline();
			_irradianceProbes.update(_scene, _computeQueue);
		}
		if(ImGui::Button("Update Probes")) {
			_irradianceProbes.update(_scene, _computeQueue);
		}
		float scale = 3.0f;
		ImGui::Text("Probes Color");
		ImGui::Image(ProbesColor,
					 ImVec2(scale * _irradianceProbes.GridParameters.colorRes * _irradianceProbes.GridParameters.resolution[0] * _irradianceProbes.GridParameters.resolution[1],
							scale * _irradianceProbes.GridParameters.colorRes * _irradianceProbes.GridParameters.resolution[2]));
		ImGui::Text("Probes Depth");
		ImGui::Image(ProbesDepth,
					 ImVec2(scale * _irradianceProbes.GridParameters.depthRes * _irradianceProbes.GridParameters.resolution[0] * _irradianceProbes.GridParameters.resolution[1],
							scale * _irradianceProbes.GridParameters.depthRes * _irradianceProbes.GridParameters.resolution[2]));
	}
	ImGui::End();

	if(ImGui::Begin("Intermediate Buffers", nullptr, ImGuiWindowFlags_HorizontalScrollbar)) {
		auto quickDisplay = [&](size_t idx) {
			if(idx > DebugTextureIDs.size()) {
				ImGui::Text("Index %d out of DebugTextureIDs bounds.", idx);
			}
			const auto& tex = DebugTextureIDs[idx];
			ImGui::Text("%s", tex.name.c_str());
			ImGui::Image(tex.id, ImVec2(_width / 2, _height / 2));
		};

		quickDisplay(_currentFrame);
		quickDisplay(_reflectionImageViews.size() + _currentFrame);
		quickDisplay(_reflectionImageViews.size() + _directLightImageViews.size() + _currentFrame * _swapChainImages.size() + 0);
		quickDisplay(_reflectionImageViews.size() + _directLightImageViews.size() + _currentFrame * _swapChainImages.size() + 1);
		quickDisplay(_reflectionImageViews.size() + _directLightImageViews.size() + _currentFrame * _swapChainImages.size() + 2);

		for(const auto& texture : DebugTextureIDs) {
			if(ImGui::TreeNode(texture.name.c_str())) {
				ImGui::Image(texture.id, ImVec2(_width / 2, _height / 2));
				ImGui::TreePop();
			}
		}
	}
	ImGui::End();

	if(ImGui::Begin("Scenes")) {
		auto&							  nodes = _scene.getNodes();
		const std::function<void(size_t)> displayNode = [&](size_t n) {
			bool open = ImGui::TreeNodeEx(makeUnique(nodes[n].name).c_str(), nodes[n].children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_OpenOnArrow);
			if(ImGui::IsItemClicked())
				SelectedNode = &nodes[n];
			if(open) {
				for(const auto& c : nodes[n].children) {
					displayNode(c);
				}
				ImGui::TreePop();
			}
		};

		for(const auto& s : _scene.getScenes()) {
			if(ImGui::TreeNode(makeUnique(s.name).c_str())) {
				for(const auto& n : s.nodes) {
					displayNode(n);
				}
				ImGui::TreePop();
			}
		}

		if(ImGui::TreeNode("Loaded Textures")) {
			for(const auto& texture : SceneUITextureIDs) {
				if(ImGui::TreeNode(texture.texture.source.string().c_str())) {
					ImGui::Image(texture.imID, ImVec2(100, 100));
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}
	}
	ImGui::End();

	if(ImGui::Begin("Node")) {
		if(SelectedNode) {
			bool dirtyMaterials = false;
			if(ImGui::TreeNode("Transform Matrix")) {
				ImGui::Matrix("Transform", SelectedNode->transform);
				ImGui::TreePop();
			}
			auto translation = glm::vec3(SelectedNode->transform[3]);
			if(ImGui::InputFloat3("Position", reinterpret_cast<float*>(&translation))) {
				SelectedNode->transform[3].x = translation.x;
				SelectedNode->transform[3].y = translation.y;
				SelectedNode->transform[3].z = translation.z;
				// TODO: Update Uniform & Acceleration Structure
			}
			if(SelectedNode->mesh != -1) {
				auto&& mesh = _scene.getMeshes()[SelectedNode->mesh];
				if(ImGui::TreeNodeEx(makeUnique(mesh.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
					for(size_t i = 0; i < mesh.SubMeshes.size(); ++i) {
						auto&& submesh = mesh.SubMeshes[i];
						if(ImGui::TreeNodeEx(makeUnique(submesh.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
							auto&& mat = Materials[submesh.materialIndex];
							if(ImGui::TreeNodeEx(makeUnique(mat.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
								++treeUniqueIdx;
								const auto texInput = [&](const char* name, uint32_t* index) {
									int tex = *index;
									if(ImGui::InputInt(name, &tex)) {
										if(tex == -1 || (tex >= 0 && tex < Textures.size())) {
											(*index) = tex;
											dirtyMaterials = true;
										}
									}
									if(*index != -1)
										ImGui::Image(SceneUITextureIDs[*index].imID, ImVec2(100, 100));
								};
								texInput("Albedo Texture", &mat.albedoTexture);
								texInput("Normal Texture", &mat.normalTexture);
								texInput("Metallic Roughness Texture", &mat.metallicRoughnessTexture);
								texInput("Emissive Texture", &mat.emissiveTexture);
								if(ImGui::ColorEdit3("Emissive Factor", reinterpret_cast<float*>(&mat.emissiveFactor))) {
									dirtyMaterials = true;
								}
								if(ImGui::SliderFloat("Metallic Factor", &mat.metallicFactor, 0.0f, 1.0f)) {
									dirtyMaterials = true;
								}
								if(ImGui::SliderFloat("Roughness Factor", &mat.roughnessFactor, 0.0f, 1.0f)) {
									dirtyMaterials = true;
								}
								ImGui::TreePop();
							}
							ImGui::TreePop();
						}
					}
					ImGui::TreePop();
				}
			}
			if(dirtyMaterials) {
				vkDeviceWaitIdle(_device); // Overkill
				writeGBufferDescriptorSets();
				uploadMaterials();		// TODO: Optimize by updating only the relevant slice
				recordCommandBuffers(); // FIXME: We're passing metalness and roughness as push constants, so we have to re-record command buffer, this should probably be part
										// of a uniform buffer (like the model matrix?)
			}
		} else {
			ImGui::Text("No selected node.");
		}
	}
	ImGui::End();

	if(ImGui::Begin("Rendering Settings")) {
		ImGui::Checkbox("Raytracing Debug", &_raytracingDebug);
		const char* values[4]{"VK_PRESENT_MODE_IMMEDIATE_KHR", "VK_PRESENT_MODE_MAILBOX_KHR", "VK_PRESENT_MODE_FIFO_KHR", "VK_PRESENT_MODE_FIFO_RELAXED_KHR"};
		int			curr_choice = static_cast<int>(_preferedPresentMode);
		if(ImGui::Combo("Present Mode", &curr_choice, values, 4)) {
			_preferedPresentMode = static_cast<VkPresentModeKHR>(curr_choice);
			_framebufferResized = true; // FIXME: Easy workaround, but can probaly be efficient.
		}
		if(ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::InputFloat3("Camera Position", reinterpret_cast<float*>(&_camera.getPosition()));
			ImGui::DragFloat("Mouse Sensitivity", &_camera.sensitivity, 0.001f, 0.001f, 100.f);
			ImGui::DragFloat("Camera Speed", &_camera.speed, 0.001f, 0.001f, 1000.f);
			float fov = _camera.getFoV();
			if(ImGui::DragFloat("Camera FoV", &fov, 1.f, 30.f, 120.f))
				_camera.setFoV(fov);
			float fnear = _camera.getNear();
			if(ImGui::DragFloat("Near Plane", &fnear, 1.f, 30.f, 120.f))
				_camera.setFoV(fnear);
			float ffar = _camera.getFar();
			if(ImGui::DragFloat("Far Plane", &ffar, 1.f, 30.f, 120.f))
				_camera.setFoV(ffar);
			ImGui::TreePop();
		}
		if(ImGui::TreeNodeEx("Light & Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Use time of day", &_deriveLightPositionFromTime);
			if(!_deriveLightPositionFromTime)
				ImGui::BeginDisabled();
			ImGui::InputFloat("Day Cycle Speed", &_dayCycleSpeed, 0.0f, 100.f);
			ImGui::InputInt("Day of the Year", &_dayOfTheYear, 0, 365);
			ImGui::InputInt("Hour", &_hour, 0, 24);
			ImGui::InputFloat("Minute", &_minute, 0.0f, 60.0f);
			ImGui::InputFloat("Longitude", &_longitude, 0.0f, 90.f);
			ImGui::InputFloat("Latitude", &_latitude, 0.0f, 90.0f);
			ImGui::InputInt("Timezone", &_utctimezone, -12, 12);
			if(!_deriveLightPositionFromTime)
				ImGui::EndDisabled();
			else
				ImGui::BeginDisabled();
			ImGui::InputFloat4("Light Direction", reinterpret_cast<float*>(&_light.direction));
			if(_deriveLightPositionFromTime)
				ImGui::EndDisabled();
			ImGui::InputFloat4("Light Color", reinterpret_cast<float*>(&_light.color));
			ImGui::TreePop();
		}
		if(ImGui::TreeNodeEx("Irradiance Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Auto. Update", &_irradianceProbeAutoUpdate);
			if(ImGui::Button("Update State")) {
				_irradianceProbes.initProbes(_computeQueue);
			}
			bool uniformNeedsUpdate = false;
			uniformNeedsUpdate = ImGui::InputFloat3("Extent Min", reinterpret_cast<float*>(&_irradianceProbes.GridParameters.extentMin)) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::InputFloat3("Extent Max", reinterpret_cast<float*>(&_irradianceProbes.GridParameters.extentMax)) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::SliderFloat("Depth Sharpness", &_irradianceProbes.GridParameters.depthSharpness, 1.0f, 100.0f) || uniformNeedsUpdate;
			ImGui::SliderFloat("Target Hysteresis", &_irradianceProbes.TargetHysteresis, 0.0f, 1.0f);
			uniformNeedsUpdate = ImGui::SliderFloat("Hysteresis", &_irradianceProbes.GridParameters.hysteresis, 0.0f, 1.0f) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::SliderFloat("Shadow Bias", &_irradianceProbes.GridParameters.shadowBias, 0.0f, 1.0f) || uniformNeedsUpdate;
			int rays = _irradianceProbes.GridParameters.raysPerProbe;
			if(ImGui::SliderInt("Rays Per Probe", &rays, 1, 128)) {
				_irradianceProbes.GridParameters.raysPerProbe = rays;
				uniformNeedsUpdate = true;
			}
			int layerPerUpdate = _irradianceProbes.GridParameters.layerPerUpdate;
			if(ImGui::SliderInt("Layer Per Update", &layerPerUpdate, 1, _irradianceProbes.GridParameters.resolution.y)) {
				while(_irradianceProbes.GridParameters.resolution.y % layerPerUpdate != 0)
					--layerPerUpdate;
				_irradianceProbes.GridParameters.layerPerUpdate = layerPerUpdate;
				uniformNeedsUpdate = true;
			}

			if(uniformNeedsUpdate)
				_irradianceProbes.updateUniforms();
			ImGui::TreePop();
		}
	}
	ImGui::End();

	if(ImGui::Begin("Statistics")) {
		if(ImPlot::BeginPlot("Frame")) {
			ImPlot::SetupAxes("Frame Number", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			plot(_frameTimes);
			ImPlot::EndPlot();
		}
		if(ImPlot::BeginPlot("Irradiance Probes")) {
			ImPlot::SetupAxes("Frame Number", "Update Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			// ImPlot::PlotLine("Update Time (ms)", _irradianceProbes.getComputeTimes().data(), static_cast<int>(_irradianceProbes.getComputeTimes().size()));
			plot(_irradianceProbes.getComputeTimes());
			ImPlot::EndPlot();
		}
	}
	ImGui::End();

	if(ImGui::Begin("Device")) {
		const auto& properties = _physicalDevice.getProperties();
		const auto& queueFamilies = _physicalDevice.getQueueFamilies();
		const auto& features = _physicalDevice.getFeatures();
		ImGui::Text(properties.deviceName);
		if(ImGui::TreeNodeEx(fmt::format("Queues ({})", queueFamilies.size()).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			for(const auto& queueFamily : queueFamilies) {
				std::string desc = std::to_string(queueFamily.queueCount);
				if(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
					desc += " Graphics";
				if(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
					desc += " Compute";
				if(queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
					desc += " Transfer";
				ImGui::Text(desc.c_str());
			}
			ImGui::TreePop();
		}
	}
	ImGui::End();
}

void Application::cleanupUI() {
	vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	_imguiCommandPool.destroy();
}
