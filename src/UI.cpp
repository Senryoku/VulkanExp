#include "Application.hpp"

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
		.PipelineCache = VK_NULL_HANDLE,
		.DescriptorPool = _imguiDescriptorPool,
		.MinImageCount = 2,
		.ImageCount = static_cast<uint32_t>(_swapChainImages.size()),
		.Allocator = VK_NULL_HANDLE,
		.CheckVkResultFn = nullptr,
	};
	ImGui_ImplVulkan_Init(&init_info, _imguiRenderPass);

	immediateSubmit([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });
	ImGui_ImplVulkan_DestroyFontUploadObjects();
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
	if(ImGui::Begin("Logs?")) {
		ImGui::End();
	}
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
		/*
		ImGui::DragFloat("Far Plane", &camera._farPlane, 1, 0.1f, 10000.f);
		ImGui::DragFloat("Far Plane", &_farPlane, 1, 0.1f, 10000.f);
		*/
		const char* values[4]{"VK_PRESENT_MODE_IMMEDIATE_KHR", "VK_PRESENT_MODE_MAILBOX_KHR", "VK_PRESENT_MODE_FIFO_KHR", "VK_PRESENT_MODE_FIFO_RELAXED_KHR"};
		int			curr_choice = static_cast<int>(_preferedPresentMode);
		if(ImGui::Combo("Present Mode", &curr_choice, values, 4)) {
			_preferedPresentMode = static_cast<VkPresentModeKHR>(curr_choice);
		}
		ImGui::End();
	}
	ImGui::ShowDemoWindow();
}

void Application::cleanupUI() {
	vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	_imguiCommandPool.destroy();
}
