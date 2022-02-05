#include "Application.hpp"

#include <ImGuiExtensions.hpp>
#include <ImGuizmo.h>
#include <implot/implot.h>

struct TextureRef {
	const Texture&	  texture;
	const ImTextureID imID;
};

static std::vector<TextureRef> SceneUITextureIDs;
static ImTextureID			   ProbesRayIrradianceDepth;
static ImTextureID			   ProbesRayDirection;
static ImTextureID			   ProbesColor;
static ImTextureID			   ProbesDepth;

struct DebugTexture {
	const std::string name;
	const ImTextureID id;
};
static std::vector<DebugTexture> DebugTextureIDs;

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

	_device.immediateSubmitGraphics([&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	for(const auto& texture : Textures) {
		SceneUITextureIDs.push_back({texture, ImGui_ImplVulkan_AddTexture(texture.sampler->getHandle(), texture.gpuImage->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	}
	ProbesRayIrradianceDepth = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getRayIrradianceDepthView(), VK_IMAGE_LAYOUT_GENERAL);
	ProbesRayDirection = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getRayDirectionView(), VK_IMAGE_LAYOUT_GENERAL);
	ProbesColor = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getIrradianceView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
		DebugTextureIDs.push_back({fmt::format("Direct Light {}", i), ImGui_ImplVulkan_AddTexture(Samplers[0], _directLightImageViews[i], VK_IMAGE_LAYOUT_GENERAL)});
	for(size_t i = 0; i < _gbufferImageViews.size(); ++i)
		DebugTextureIDs.push_back({fmt::format("GBuffer {}", i), ImGui_ImplVulkan_AddTexture(Samplers[0], _gbufferImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	for(size_t i = 0; i < _reflectionIntermediateFilterImageViews.size(); ++i)
		DebugTextureIDs.push_back(
			{fmt::format("Reflection Filtered {}", i), ImGui_ImplVulkan_AddTexture(Samplers[0], _reflectionIntermediateFilterImageViews[i], VK_IMAGE_LAYOUT_GENERAL)});
}

template<class T>
void plot(const char* name, const RollingBuffer<T>& rb) {
	auto data = rb.get();
	ImPlot::PlotLine(name, data.first, static_cast<int>(data.firstCount));
	ImPlot::PlotLine(name, data.second, static_cast<int>(data.secondCount), 1.0, data.firstCount);
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
		ImGui::Text("Probes Ray Irradiance Depth");
		ImGui::Image(ProbesRayIrradianceDepth,
					 ImVec2(scale * _irradianceProbes.GridParameters.resolution[0] * _irradianceProbes.GridParameters.resolution[2], scale * _irradianceProbes.MaxRaysPerProbe));
		ImGui::Text("Probes Ray Direction");
		ImGui::Image(ProbesRayDirection,
					 ImVec2(scale * _irradianceProbes.GridParameters.resolution[0] * _irradianceProbes.GridParameters.resolution[2], scale * _irradianceProbes.MaxRaysPerProbe));
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

	if(ImGui::Begin("Scene")) {
		auto&										nodes = _scene.getNodes();
		const std::function<void(Scene::NodeIndex)> displayNode = [&](Scene::NodeIndex n) {
			bool open = ImGui::TreeNodeEx(makeUnique(nodes[n].name).c_str(), nodes[n].children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_OpenOnArrow);
			// Drag & Drop nodes to edit parent/children links
			// TODO: Allow re-ordering between children (needs dummy ).
			if(ImGui::BeginDragDropTarget()) {
				auto payload = ImGui::AcceptDragDropPayload("NodeIndex");
				if(payload) {
					auto droppedNode = *static_cast<Scene::NodeIndex*>(payload->Data);
					if(droppedNode != n) {
						// Remove previous parent-child link
						nodes[nodes[droppedNode].parent].children.erase(
							std::find(nodes[nodes[droppedNode].parent].children.begin(), nodes[nodes[droppedNode].parent].children.end(), droppedNode));
						nodes[droppedNode].parent = Scene::InvalidNodeIndex;
						_scene.addChild(n, droppedNode);
					}
				}
				ImGui::EndDragDropTarget();
			}
			if(ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload("NodeIndex", &n, sizeof(n));
				ImGui::EndDragDropSource();
			}

			if(ImGui::IsItemClicked())
				_selectedNode = n;
			if(open) {
				for(const auto& c : nodes[n].children) {
					displayNode(c);
				}
				ImGui::TreePop();
			}
		};
		displayNode(Scene::NodeIndex{0});

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

	bool					   updatedTransform = false;
	static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
	static ImGuizmo::MODE	   mCurrentGizmoMode(ImGuizmo::LOCAL);
	static bool				   useSnap(false);
	glm::vec3				   snap{1.0};

	if(ImGui::Begin("Node")) {
		if(_selectedNode != Scene::InvalidNodeIndex) {
			// TEMP Button
			// TODO: Ctrl+D shortcut?
			if(ImGui::Button("Duplicate")) {
				std::function<Scene::NodeIndex(Scene::NodeIndex)> copyNode = [&](Scene::NodeIndex target) {
					_scene.getNodes().push_back(_scene[target]);
					auto&			 copy = _scene.getNodes().back();
					Scene::NodeIndex index(_scene.getNodes().size() - 1);
					copy.parent = Scene::InvalidNodeIndex;
					copy.children.clear();
					for(const auto& c : _scene[target].children) {
						auto childIndex = copyNode(c);
						_scene.addChild(index, childIndex);
					}
					return index;
				};

				auto parent = _scene[_selectedNode].parent;
				_selectedNode = copyNode(_selectedNode);
				_scene.addChild(parent, _selectedNode);

				// Recreate Acceleration Structure
				// FIXME: This should abstracted away, like simply setting a flag and letting the main loop update the structures.
				vkDeviceWaitIdle(_device);
				_scene.destroyAccelerationStructure(_device);
				_scene.createAccelerationStructure(_device);
				// We have to update the all descriptor sets referencing the acceleration structures.
				// FIXME: This is way overkill
				recreateSwapChain();
				vkDeviceWaitIdle(_device);
			}

			bool dirtyMaterials = false;
			if(ImGui::TreeNodeEx("Transform Matrix", ImGuiTreeNodeFlags_Leaf)) {
				float matrixTranslation[3], matrixRotation[3], matrixScale[3];
				ImGui::Matrix("Local Transform", _scene[_selectedNode].transform);
				ImGuizmo::DecomposeMatrixToComponents(reinterpret_cast<float*>(&_scene[_selectedNode].transform), matrixTranslation, matrixRotation, matrixScale);
				updatedTransform = ImGui::InputFloat3("Translation", matrixTranslation) || updatedTransform;
				updatedTransform = ImGui::InputFloat3("Rotation   ", matrixRotation) || updatedTransform;
				updatedTransform = ImGui::InputFloat3("Scale      ", matrixScale) || updatedTransform;
				if(updatedTransform)
					ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, reinterpret_cast<float*>(&_scene[_selectedNode].transform));

				if(ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
					mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
				ImGui::SameLine();
				if(ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
					mCurrentGizmoOperation = ImGuizmo::ROTATE;
				ImGui::SameLine();
				if(ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
					mCurrentGizmoOperation = ImGuizmo::SCALE;

				if(mCurrentGizmoOperation != ImGuizmo::SCALE) {
					if(ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
						mCurrentGizmoMode = ImGuizmo::LOCAL;
					ImGui::SameLine();
					if(ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
						mCurrentGizmoMode = ImGuizmo::WORLD;
				}
				if(ImGui::IsKeyPressed(GLFW_KEY_X))
					useSnap = !useSnap;
				ImGui::Checkbox("", &useSnap);
				ImGui::SameLine();
				switch(mCurrentGizmoOperation) {
					case ImGuizmo::TRANSLATE:
						// snap = config.mSnapTranslation;
						ImGui::InputFloat3("Snap", &snap.x);
						break;
					case ImGuizmo::ROTATE:
						// snap = config.mSnapRotation;
						ImGui::InputFloat("Angle Snap", &snap.x);
						break;
					case ImGuizmo::SCALE:
						// snap = config.mSnapScale;
						ImGui::InputFloat("Scale Snap", &snap.x);
						break;
				}

				ImGui::TreePop();
			}
			if(_scene[_selectedNode].mesh != Scene::InvalidMeshIndex) {
				auto&& mesh = _scene[_scene[_selectedNode].mesh];
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
								if(ImGui::ColorEdit3("Base Color", reinterpret_cast<float*>(&mat.properties.baseColorFactor))) {
									dirtyMaterials = true;
								}
								texInput("Albedo Texture", &mat.properties.albedoTexture);
								texInput("Normal Texture", &mat.properties.normalTexture);
								texInput("Metallic Roughness Texture", &mat.properties.metallicRoughnessTexture);
								texInput("Emissive Texture", &mat.properties.emissiveTexture);
								if(ImGui::ColorEdit3("Emissive Factor", reinterpret_cast<float*>(&mat.properties.emissiveFactor))) {
									dirtyMaterials = true;
								}
								if(ImGui::SliderFloat("Metallic Factor", &mat.properties.metallicFactor, 0.0f, 1.0f)) {
									dirtyMaterials = true;
								}
								if(ImGui::SliderFloat("Roughness Factor", &mat.properties.roughnessFactor, 0.0f, 1.0f)) {
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

	// On screen gizmos
	if(_selectedNode != Scene::InvalidNodeIndex) {
		glm::mat4		 worldTransform = _scene[_selectedNode].transform;
		glm::mat4		 parentTransform{1.0f};
		Scene::NodeIndex parentNode = _scene[_selectedNode].parent;
		while(parentNode != Scene::InvalidNodeIndex) {
			worldTransform = _scene[parentNode].transform * worldTransform;
			parentTransform = _scene[parentNode].transform * parentTransform;
			parentNode = _scene[parentNode].parent;
		}

		// Dummy Window for "on field" widgets
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2{static_cast<float>(_width), static_cast<float>(_height)});
		ImGui::SetNextWindowBgAlpha(0.0);
		ImGui::Begin("SelectedObject", nullptr,
					 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

		if(_scene[_selectedNode].mesh != Scene::InvalidMeshIndex) {
			auto				  aabb = _scene[_scene[_selectedNode].mesh].getBounds().getPoints();
			auto				  winpos = ImGui::GetMainViewport()->Pos;
			glm::vec2			  glmwinpos{winpos.x, winpos.y};
			std::array<ImVec2, 8> screen_aabb;
			for(int i = 0; i < 8; ++i) {
				auto t = _camera.getViewMatrix() * worldTransform * glm::vec4(aabb[i], 1.0);
				if(t.z > 0.0) // Truncate is point is behind camera
					t.z = 0.0;
				t = _camera.getProjectionMatrix() * t;
				auto r = glm::vec2{t.x, -t.y} / t.w;
				r = 0.5f * (r + 1.0f);
				r.x *= _width;
				r.y *= _height;
				screen_aabb[i] = r + glmwinpos;
			}
			// Bounding Box Gizmo
			constexpr std::array<size_t, 24> segments{0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7};
			ImDrawList*						 drawlist = ImGui::GetWindowDrawList();
			for(int i = 0; i < 24; i += 2)
				drawlist->AddLine(screen_aabb[segments[i]], screen_aabb[segments[i + 1]], ImGui::ColorConvertFloat4ToU32(ImVec4(0.0, 0.0, 1.0, 0.5)));
		}

		if(ImGui::IsKeyPressed(GLFW_KEY_T) || ImGui::IsKeyPressed(GLFW_KEY_Z))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		if(ImGui::IsKeyPressed(GLFW_KEY_R))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		if(ImGui::IsKeyPressed(GLFW_KEY_Y))
			mCurrentGizmoOperation = ImGuizmo::SCALE;
		ImGuiIO& io = ImGui::GetIO();
		int		 x, y;
		glfwGetWindowPos(_window, &x, &y);
		ImGuizmo::SetRect(x, y, io.DisplaySize.x, io.DisplaySize.y);
		glm::mat4 delta;
		bool	  gizmoUpdated =
			ImGuizmo::Manipulate(reinterpret_cast<const float*>(&_camera.getViewMatrix()), reinterpret_cast<const float*>(&_camera.getProjectionMatrix()), mCurrentGizmoOperation,
								 mCurrentGizmoMode, reinterpret_cast<float*>(&worldTransform), reinterpret_cast<float*>(&delta), useSnap ? &snap.x : nullptr);
		if(gizmoUpdated) {
			_scene[_selectedNode].transform = glm::inverse(parentTransform) * worldTransform;
		}
		updatedTransform = gizmoUpdated || updatedTransform;

		ImGui::End(); // Dummy Window
	}

	if(_selectedNode != Scene::InvalidNodeIndex && updatedTransform) {
		_scene.markDirty(_selectedNode);
	}

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
			if(ImGui::DragFloat("Near Plane", &fnear, 1.f, 0.f, 100.f))
				_camera.setNear(fnear);
			float ffar = _camera.getFar();
			if(ImGui::DragFloat("Far Plane", &ffar, 1.f, 100.f, 40000.f))
				_camera.setFar(ffar);
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
			if(ImGui::Button("Fit to Scene")) {
				_scene.computeBounds();
				_irradianceProbes.GridParameters.extentMin = _scene.getBounds().min;
				_irradianceProbes.GridParameters.extentMax = _scene.getBounds().max;
				uniformNeedsUpdate = true;
			}
			uniformNeedsUpdate = ImGui::InputFloat3("Extent Min", reinterpret_cast<float*>(&_irradianceProbes.GridParameters.extentMin)) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::InputFloat3("Extent Max", reinterpret_cast<float*>(&_irradianceProbes.GridParameters.extentMax)) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::SliderFloat("Depth Sharpness", &_irradianceProbes.GridParameters.depthSharpness, 1.0f, 100.0f) || uniformNeedsUpdate;
			ImGui::SliderFloat("Target Hysteresis", &_irradianceProbes.TargetHysteresis, 0.0f, 1.0f);
			uniformNeedsUpdate = ImGui::SliderFloat("Hysteresis", &_irradianceProbes.GridParameters.hysteresis, 0.0f, 1.0f) || uniformNeedsUpdate;
			uniformNeedsUpdate = ImGui::SliderFloat("Shadow Bias", &_irradianceProbes.GridParameters.shadowBias, 0.0f, 100.0f) || uniformNeedsUpdate;
			int rays = _irradianceProbes.GridParameters.raysPerProbe;
			if(ImGui::SliderInt("Rays Per Probe", &rays, 1, IrradianceProbes::MaxRaysPerProbe)) {
				_irradianceProbes.GridParameters.raysPerProbe = rays;
				uniformNeedsUpdate = true;
			}
			int probesPerUpdate = _irradianceProbes.ProbesPerUpdate;
			if(ImGui::SliderInt("Probes Per Update", &probesPerUpdate, 0, _irradianceProbes.getProbeCount())) {
				_irradianceProbes.ProbesPerUpdate = probesPerUpdate;
			}

			if(uniformNeedsUpdate)
				_irradianceProbes.updateUniforms();
			ImGui::TreePop();
		}
	}
	ImGui::End();

	if(ImGui::Begin("Statistics")) {
		if(ImPlot::BeginPlot("Time between Presents")) {
			ImPlot::SetupAxes("Frame Number", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			plot("Frame Time", _presentTimes);
			ImPlot::EndPlot();
		}
		if(ImPlot::BeginPlot("Main Render")) {
			ImPlot::SetupAxes("Frame Number", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			plot("Full Time", _frameTimes);
			plot("GBuffer Time", _gbufferTimes);
			plot("Reflection & Direct Light Time", _reflectionDirectLightTimes);
			plot("Reflection Filter Time", _reflectionFilterTimes);
			plot("Gather Time", _gatherTimes);
			ImPlot::EndPlot();
		}
		if(ImPlot::BeginPlot("Irradiance Probes")) {
			ImPlot::SetupAxes("Frame Number", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			plot("Full Time", _irradianceProbes.getComputeTimes());
			plot("Trace Time", _irradianceProbes.getTraceTimes());
			plot("Update Time", _irradianceProbes.getUpdateTimes());
			plot("Border Copy", _irradianceProbes.getBorderCopyTimes());
			plot("Copy", _irradianceProbes.getCopyTimes());
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
