#include "Application.hpp"

#include <ImGuiExtensions.hpp>

struct TextureRef {
	const Texture const& texture;
	const ImTextureID	 imID;
};

std::vector<TextureRef> SceneUITextureIDs;
ImTextureID				ProbesColor;
ImTextureID				ProbesDepth;

struct DebugTexture {
	const std::string name;
	const ImTextureID id;
};
std::vector<DebugTexture> DebugTextureIDs;

void Application::initImGui(uint32_t queueFamily) {
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;	// Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
	// io.ConfigViewportsNoAutoMerge = true;
	// io.ConfigViewportsNoTaskBarIcon = true;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.2f;
	// ImGui::StyleColorsClassic();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
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

	immediateSubmit([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });
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
	for(size_t i = 0; i < _gbufferImageViews.size(); ++i)
		DebugTextureIDs.push_back({fmt::format("GBuffer {}", i), ImGui_ImplVulkan_AddTexture(Samplers[0], _gbufferImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
}

void Application::drawUI() {
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
			_irradianceProbes.update(_scene, _graphicsQueue);
		}
		if(ImGui::Button("Update Probes")) {
			_irradianceProbes.update(_scene, _graphicsQueue);
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
		quickDisplay(_reflectionImageViews.size() + _currentFrame * _swapChainImages.size() + 0);
		quickDisplay(_reflectionImageViews.size() + _currentFrame * _swapChainImages.size() + 1);
		quickDisplay(_reflectionImageViews.size() + _currentFrame * _swapChainImages.size() + 2);

		for(const auto& texture : DebugTextureIDs) {
			if(ImGui::TreeNode(texture.name.c_str())) {
				ImGui::Image(texture.id, ImVec2(_width / 2, _height / 2));
				ImGui::TreePop();
			}
		}
	}
	ImGui::End();

	if(ImGui::Begin("Scenes", nullptr, ImGuiWindowFlags_NoBackground /* FIXME: Doesn't work. */)) {
		const auto						  nodes = _scene.getNodes();
		const std::function<void(size_t)> displayNode = [&](size_t n) {
			if(ImGui::TreeNode((nodes[n].name + "##" + std::to_string(n)).c_str())) {
				ImGui::Matrix("Transform", nodes[n].transform);
				if(nodes[n].mesh != -1)
					ImGui::Text("Mesh: %s", _scene.getMeshes()[nodes[n].mesh].name.c_str());
				for(const auto& c : nodes[n].children) {
					displayNode(c);
				}
				ImGui::TreePop();
			}
		};

		for(const auto& s : _scene.getScenes()) {
			if(ImGui::TreeNode(s.name.c_str())) {
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

	ImGui::SetNextWindowBgAlpha(0.35f); // FIXME: Doesn't work.
	if(ImGui::Begin("Rendering Settings")) {
		ImGui::InputFloat3("Camera Position", reinterpret_cast<float*>(&_camera.getPosition()));
		ImGui::Checkbox("Raytracing Debug", &_raytracingDebug);
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
		const char* values[4]{"VK_PRESENT_MODE_IMMEDIATE_KHR", "VK_PRESENT_MODE_MAILBOX_KHR", "VK_PRESENT_MODE_FIFO_KHR", "VK_PRESENT_MODE_FIFO_RELAXED_KHR"};
		int			curr_choice = static_cast<int>(_preferedPresentMode);
		if(ImGui::Combo("Present Mode", &curr_choice, values, 4)) {
			_preferedPresentMode = static_cast<VkPresentModeKHR>(curr_choice);
			_framebufferResized = true; // FIXME: Easy workaround, but can probaly be efficient.
		}
		if(ImGui::TreeNodeEx("Irradiance Probes", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Auto. Update", &_irradianceProbeAutoUpdate);
			bool uniformNeedsUpdate = false;
			uniformNeedsUpdate = ImGui::InputFloat3("Extent Min", reinterpret_cast<float*>(&_irradianceProbes.GridParameters.extentMin)) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::InputFloat3("Extent Max", reinterpret_cast<float*>(&_irradianceProbes.GridParameters.extentMax)) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::SliderFloat("Depth Sharpness", &_irradianceProbes.GridParameters.depthSharpness, 1.0f, 100.0f) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::SliderFloat("Hysteresis", &_irradianceProbes.GridParameters.hysteresis, 0.0f, 1.0f) || uniformNeedsUpdate;
			int rays = _irradianceProbes.GridParameters.raysPerProbe;
			if(ImGui::SliderInt("Rays Per Probe", &rays, 1, 128)) {
				_irradianceProbes.GridParameters.raysPerProbe = rays;
				uniformNeedsUpdate = true;
			}

			if(uniformNeedsUpdate)
				_irradianceProbes.updateUniforms();
			ImGui::TreePop();
		}
	}
	ImGui::End();
}

void Application::cleanupUI() {
	vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	_imguiCommandPool.destroy();
}
