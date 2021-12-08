#include "Application.hpp"

#include <ImGuiExtensions.hpp>

std::vector<ImTextureID> SceneUITextureIDs;
ImTextureID				 ProbesColor;
ImTextureID				 ProbesDepth;

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
	if(vkCreateDescriptorPool(_device, &pool_info, nullptr, &_imguiDescriptorPool))
		throw std::runtime_error("Failed to create dear imgui descriptor pool.");

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
		SceneUITextureIDs.push_back(ImGui_ImplVulkan_AddTexture(texture.sampler->getHandle(), texture.gpuImage->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
	}
	ProbesColor = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getColorView(), VK_IMAGE_LAYOUT_GENERAL);
	ProbesDepth = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getDepthView(), VK_IMAGE_LAYOUT_GENERAL);
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
	if(ImGui::Begin("Debug")) {
		ImGui::Text("Probes Color");
		ImGui::Image(ProbesColor, ImVec2(_irradianceProbes.ColorResolution * _irradianceProbes.VolumeResolution[0] * _irradianceProbes.VolumeResolution[1],
										 _irradianceProbes.ColorResolution * _irradianceProbes.VolumeResolution[2]));
		ImGui::Text("Probes Depth");
		ImGui::Image(ProbesDepth, ImVec2(_irradianceProbes.DepthResolution * _irradianceProbes.VolumeResolution[0] * _irradianceProbes.VolumeResolution[1],
										 _irradianceProbes.DepthResolution * _irradianceProbes.VolumeResolution[2]));
		ImGui::End();
	}
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

		ImGui::Text("Loaded Textures");
		size_t n = 0;
		for(const auto& texture : SceneUITextureIDs) {
			if(ImGui::TreeNode(std::to_string(n).c_str())) {
				ImGui::Image(texture, ImVec2(100, 100));
				ImGui::TreePop();
			}
			++n;
		}
		ImGui::End();
	}
	ImGui::SetNextWindowBgAlpha(0.35f); // FIXME: Doesn't work.
	if(ImGui::Begin("Rendering Settings")) {

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
		ImGui::End();
	}
}

void Application::cleanupUI() {
	vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	_imguiCommandPool.destroy();
}
