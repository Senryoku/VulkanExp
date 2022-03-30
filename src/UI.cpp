#include "Editor.hpp"

#include <ImGuiExtensions.hpp>
#include <ImGuizmo.h>
#include <implot/implot.h>
#include <misc/cpp/imgui_stdlib.h>

struct TextureRef {
	const TextureIndex textureIndex;
	const ImTextureID  imID;
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

void Editor::initImGui(uint32_t queueFamily) {
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

	for(TextureIndex i = TextureIndex{0}; i < TextureIndex(Textures.size()); ++i) {
		SceneUITextureIDs.push_back({i, ImGui_ImplVulkan_AddTexture(Textures[i].sampler->getHandle(), Textures[i].gpuImage->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	}
	ProbesRayIrradianceDepth = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getRayIrradianceDepthView(), VK_IMAGE_LAYOUT_GENERAL);
	ProbesRayDirection = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getRayDirectionView(), VK_IMAGE_LAYOUT_GENERAL);
	ProbesColor = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getIrradianceView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	ProbesDepth = ImGui_ImplVulkan_AddTexture(Samplers[0], _irradianceProbes.getDepthView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Editor::createImGuiRenderPass() {
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

void Editor::uiOnTextureChange() {
	// Prepare new scene textures for display
	for(TextureIndex i = TextureIndex(SceneUITextureIDs.size()); i < Textures.size(); ++i) {
		SceneUITextureIDs.push_back({i, ImGui_ImplVulkan_AddTexture(Textures[i].sampler->getHandle(), Textures[i].gpuImage->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	}

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
void Editor::recordUICommandBuffer(size_t imageIndex) {
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

void Editor::drawUI() {
	size_t	   treeUniqueIdx = 0;
	const auto makeUnique = [&](const std::string& name) { return (name + "##" + std::to_string(++treeUniqueIdx)); };

	const auto displayMaterial = [&](MaterialIndex* matIdx, bool dropTarget = false) {
		bool modified = false;
		if(*matIdx == InvalidMaterialIndex) {
			ImGui::Text("No Material.");
			if(dropTarget && ImGui::BeginDragDropTarget()) {
				auto payload = ImGui::AcceptDragDropPayload("MaterialIndex");
				if(payload) {
					auto droppedMat = *static_cast<MaterialIndex*>(payload->Data);
					*matIdx = droppedMat;
					_renderer.updateMeshOffsetTable();
					_renderer.uploadMeshOffsetTable();
					_outdatedCommandBuffers = true;
				}
				ImGui::EndDragDropTarget();
			}
			return false;
		}
		auto& mat = Materials[*matIdx];
		if(ImGui::TreeNodeEx(makeUnique(mat.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			if(dropTarget && ImGui::BeginDragDropTarget()) {
				auto payload = ImGui::AcceptDragDropPayload("MaterialIndex");
				if(payload) {
					auto droppedMat = *static_cast<MaterialIndex*>(payload->Data);
					*matIdx = droppedMat;
					_renderer.updateMeshOffsetTable();
					_renderer.uploadMeshOffsetTable();
					_outdatedCommandBuffers = true;
				}
				ImGui::EndDragDropTarget();
			}
			if(ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload("MaterialIndex", matIdx, sizeof(MaterialIndex));
				ImGui::EndDragDropSource();
			}

			++treeUniqueIdx;
			const auto texInput = [&](const char* name, uint32_t* index) {
				int tex = *index;
				if(ImGui::InputInt(name, &tex)) {
					if(tex == -1 || (tex >= 0 && tex < Textures.size())) {
						(*index) = tex;
						modified = true;
					}
				}
				if(*index != -1)
					ImGui::Image(SceneUITextureIDs[*index].imID, ImVec2(100, 100));
			};
			if(ImGui::ColorEdit3("Base Color", reinterpret_cast<float*>(&mat.properties.baseColorFactor))) {
				modified = true;
			}
			texInput("Albedo Texture", &mat.properties.albedoTexture);
			texInput("Normal Texture", &mat.properties.normalTexture);
			texInput("Metallic Roughness Texture", &mat.properties.metallicRoughnessTexture);
			texInput("Emissive Texture", &mat.properties.emissiveTexture);
			if(ImGui::ColorEdit3("Emissive Factor", reinterpret_cast<float*>(&mat.properties.emissiveFactor))) {
				modified = true;
			}
			if(ImGui::SliderFloat("Metallic Factor", &mat.properties.metallicFactor, 0.0f, 1.0f)) {
				modified = true;
			}
			if(ImGui::SliderFloat("Roughness Factor", &mat.properties.roughnessFactor, 0.0f, 1.0f)) {
				modified = true;
			}
			ImGui::TreePop();
		}
		return modified;
	};

	bool updatedTransform = false;

	// On screen gizmos
	if(_selectedNode != entt::null) {
		auto&		 node = _scene.getRegistry().get<NodeComponent>(_selectedNode);
		glm::mat4	 worldTransform = node.transform;
		glm::mat4	 parentTransform{1.0f};
		entt::entity parent = node.parent;
		while(parent != entt::null) {
			auto parentNode = _scene.getRegistry().get<NodeComponent>(parent);
			worldTransform = parentNode.transform * worldTransform;
			parentTransform = parentNode.transform * parentTransform;
			parent = parentNode.parent;
		}

		// Dummy Window for "on field" widgets
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2{static_cast<float>(_width), static_cast<float>(_height)});
		ImGui::SetNextWindowBgAlpha(0.0);
		ImGui::Begin("SelectedObject", nullptr,
					 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

		auto	  winpos = ImGui::GetMainViewport()->Pos;
		glm::vec2 glmwinpos{winpos.x, winpos.y};
		auto	  project = [&](const glm::mat4& transform, const glm::vec3& pos = glm::vec3(0)) {
			 auto t = _camera.getViewMatrix() * transform * glm::vec4(pos, 1.0);
			 if(t.z > 0.0) // Truncate is point is behind camera
				 t.z = 0.0;
			 t = _camera.getProjectionMatrix() * t;
			 auto r = glm::vec2{t.x, -t.y} / t.w;
			 r = 0.5f * (r + 1.0f);
			 r.x *= _width;
			 r.y *= _height;
			 return r + glmwinpos;
		};

		auto* mesh = _scene.getRegistry().try_get<MeshRendererComponent>(_selectedNode);
		auto* skinnedMesh = _scene.getRegistry().try_get<SkinnedMeshRendererComponent>(_selectedNode);
		// Display Bounds
		if(mesh || skinnedMesh) {
			auto				  aabb = _scene[mesh ? mesh->meshIndex : skinnedMesh->meshIndex].getBounds().getPoints();
			std::array<ImVec2, 8> screen_aabb;
			for(int i = 0; i < 8; ++i)
				screen_aabb[i] = project(worldTransform, aabb[i]);
			// Bounding Box Gizmo
			constexpr std::array<size_t, 24> segments{0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7};
			ImDrawList*						 drawlist = ImGui::GetWindowDrawList();
			for(int i = 0; i < 24; i += 2)
				drawlist->AddLine(screen_aabb[segments[i]], screen_aabb[segments[i + 1]], ImGui::ColorConvertFloat4ToU32(ImVec4(0.0, 0.0, 1.0, 0.5)));
		}
		// Display Bones & Joints
		auto* animationComponent = _scene.getRegistry().try_get<AnimationComponent>(_selectedNode);
		if(skinnedMesh || animationComponent) {
			std::vector<entt::entity> joints;
			if(skinnedMesh)
				joints = _scene.getSkins()[skinnedMesh->skinIndex].joints;
			else
				for(const auto& na : Animations[animationComponent->animationIndex].nodeAnimations)
					joints.push_back(na.first);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			for(const auto& entity : joints) {
				const auto& node = _scene.getRegistry().get<NodeComponent>(entity);
				auto		transform = node.globalTransform * glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
				ImGuizmo::DrawCubes(&_camera.getViewMatrix()[0][0], &_camera.getProjectionMatrix()[0][0], &transform[0][0], 1);
				auto child = node.first;
				auto parentPixel = project(transform);
				while(child != entt::null) {
					auto childNode = _scene.getRegistry().get<NodeComponent>(child);
					auto childTransform = childNode.globalTransform;
					auto childPixel = project(childTransform);
					drawList->AddLine(ImVec2(parentPixel), ImVec2(childPixel), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0, 1.0, 0.0, 0.5)));
					child = childNode.next;
				}
			}
		}

		ImGuiIO& io = ImGui::GetIO();
		int		 x, y;
		glfwGetWindowPos(_window, &x, &y);
		ImGuizmo::SetRect(x, y, io.DisplaySize.x, io.DisplaySize.y);
		glm::mat4 delta;
		bool	  gizmoUpdated =
			ImGuizmo::Manipulate(reinterpret_cast<const float*>(&_camera.getViewMatrix()), reinterpret_cast<const float*>(&_camera.getProjectionMatrix()), _currentGizmoOperation,
								 _currentGizmoMode, reinterpret_cast<float*>(&worldTransform), reinterpret_cast<float*>(&delta), _useSnap ? &_snapOffset.x : nullptr);
		if(gizmoUpdated) {
			node.transform = glm::inverse(parentTransform) * worldTransform;
		}
		updatedTransform = gizmoUpdated || updatedTransform;

		ImGui::End(); // Dummy Window
	}

	if(_selectedNode != entt::null && updatedTransform) {
		_scene.markDirty(_selectedNode);
	}

	if(ImGui::Begin("Animation")) {
		if(_selectedNode == entt::null) {
			ImGui::Text("No selected node.");
		} else {
			AnimationComponent* animComp = _scene.getRegistry().try_get<AnimationComponent>(_selectedNode);
			if(!animComp) {
				ImGui::Text("Selected node doesn't have an Animation Component.");
			} else if(animComp->animationIndex == InvalidAnimationIndex || animComp->animationIndex >= Animations.size()) {
				ImGui::Text("Animation Component doesn't refer to a valid animation.");
			} else {
				auto& anim = Animations[animComp->animationIndex];
				if(ImGui::Button("Play")) {
					animComp->running = true;
				}
				ImGui::SameLine();
				if(ImGui::Button("Pause")) {
					animComp->running = false;
				}
				static entt::entity		  selectedAnimationNode = entt::null;
				static int				  currentNodeIndex = 0;
				std::vector<entt::entity> nodes;
				std::vector<const char*>  nodeNames;
				for(auto& n : anim.nodeAnimations) {
					nodes.push_back(n.first);
					nodeNames.push_back(_scene.getRegistry().get<NodeComponent>(n.first).name.c_str());
				}
				if(selectedAnimationNode == entt::null && !nodes.empty())
					selectedAnimationNode = nodes[0];
				else if(nodes.empty())
					selectedAnimationNode = entt::null;
				if(ImGui::Combo("Animation Node", &currentNodeIndex, nodeNames.data(), anim.nodeAnimations.size()))
					selectedAnimationNode = nodes[currentNodeIndex];
				if(selectedAnimationNode != entt::null && anim.nodeAnimations.contains(selectedAnimationNode)) {
					if(ImGui::BeginTabBar("#Track")) {
						auto& na = anim.nodeAnimations[selectedAnimationNode];
						auto  duration = na.rotationKeyFrames.times.back(); // FIXME
						if(ImGui::BeginTabItem("Position")) {
							if(na.translationKeyFrames.times.size() > 0) {
								// TODO
							}
							ImGui::EndTabItem();
						}
						if(ImGui::BeginTabItem("Rotation")) {
							if(na.rotationKeyFrames.times.size() > 0) {
								ImPlotAxisFlags ax_flags = ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks;
								if(ImPlot::BeginPlot("##Rotation", ImVec2(-1, 0), ImPlotFlags_CanvasOnly | ImPlotFlags_NoInputs)) {
									ImPlot::SetupAxes(0, 0, ax_flags, ax_flags);
									ImPlot::SetupAxesLimits(0, duration, -180, 180);

									double time = std::fmod(animComp->time, duration);
									ImPlot::SetNextLineStyle(ImVec4(1, 1, 1, 1));
									ImPlot::PlotVLines("Time", &time, 1);
									ImPlot::TagX(time, ImVec4(1, 1, 1, 1));
									const static ImVec4 axisColors[3]{
										ImVec4(1, 0, 0, 1),
										ImVec4(0, 1, 0, 1),
										ImVec4(0, 0, 1, 1),
									};
									for(size_t c = 0; c < 3; ++c) {
										std::vector<ImPlotPoint> points;
										for(size_t i = 0; i < na.rotationKeyFrames.times.size(); ++i) {
											auto		euler = glm::eulerAngles(na.rotationKeyFrames.frames[i]);
											ImPlotPoint point{na.rotationKeyFrames.times[i], 360.0f / (2.0f * glm::pi<float>()) * euler[c]};
											if(ImPlot::DragPoint(c * na.rotationKeyFrames.times.size() + i, &point.x, &point.y, axisColors[c], 4)) {
												euler[c] = point.y / (360.0f / (2.0f * glm::pi<float>()));
												na.rotationKeyFrames.frames[i] = glm::quat(euler);

												animComp->running = false;
												animComp->forceUpdate = true;
												animComp->time = na.rotationKeyFrames.times[i];
											}
											points.push_back(point);
										}
										ImPlot::SetNextLineStyle(axisColors[c]);
										ImPlot::PlotLine("##h1", &points[0].x, &points[0].y, points.size(), 0, sizeof(ImPlotPoint));
									}

									ImPlot::EndPlot();
								}
							}
							ImGui::EndTabItem();
						}
						ImGui::EndTabBar();
					}
				}
			}
		}
	}
	ImGui::End();

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

	bool dirtyMaterials = false;

	if(ImGui::Begin("Objects")) {
		const std::function<void(entt::entity)> displayNode = [&](entt::entity entity) {
			auto& n = _scene.getRegistry().get<NodeComponent>(entity);

			bool open =
				ImGui::TreeNodeEx(makeUnique(n.name).c_str(), (entity == _selectedNode ? ImGuiTreeNodeFlags_Selected : 0) |
																  (n.children == 0 ? ImGuiTreeNodeFlags_Leaf : (ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen)));
			// Drag & Drop nodes to edit parent/children links
			// TODO: Allow re-ordering between children (needs dummy?).
			if(ImGui::BeginDragDropTarget()) {
				auto payload = ImGui::AcceptDragDropPayload("Entity");
				if(payload) {
					auto droppedEntity = *static_cast<entt::entity*>(payload->Data);
					if(droppedEntity != n) {
						// Make sure entity is not a descendant of droppedEntity
						bool  isDescendant = false;
						auto* currNode = &_scene.getRegistry().get<NodeComponent>(entity);
						while(currNode->parent != entt::null) {
							if(currNode->parent == droppedEntity) {
								isDescendant = true;
								break;
							}
							currNode = &_scene.getRegistry().get<NodeComponent>(currNode->parent);
						}
						if(!isDescendant) {
							// TODO: Adjust local transform so the world one stays the same?
							_scene.removeFromHierarchy(droppedEntity);
							_scene.addChild(entity, droppedEntity);
							_dirtyHierarchy = true;
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if(ImGui::BeginDragDropSource()) {
				ImGui::SetDragDropPayload("Entity", &entity, sizeof(entity));
				ImGui::EndDragDropSource();
			}

			if(ImGui::IsItemClicked())
				_selectedNode = entity;
			if(open) {
				for(auto c = n.first; c != entt::null; c = _scene.getRegistry().get<NodeComponent>(c).next)
					displayNode(c);
				ImGui::TreePop();
			}
		};
		displayNode(_scene.getRoot());
	}
	ImGui::End();

	if(ImGui::Begin("Textures")) {
		for(const auto& texture : SceneUITextureIDs) {
			if(ImGui::TreeNode(Textures[texture.textureIndex].source.string().c_str())) {
				ImGui::Image(texture.imID, ImVec2(100, 100));
				ImGui::TreePop();
			}
		}
	}
	ImGui::End();

	if(ImGui::Begin("Materials")) {
		for(MaterialIndex i = MaterialIndex(0u); i < Materials.size(); ++i) {
			dirtyMaterials = displayMaterial(&i, false) || dirtyMaterials;
		}
	}
	ImGui::End();

	if(ImGui::Begin("Meshes")) {
		for(MeshIndex i = MeshIndex(0u); i < _scene.getMeshes().size(); ++i) {
			auto& m = _scene.getMeshes()[i];
			if(ImGui::TreeNodeEx(makeUnique(m.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Text("Vertices: %d", m.getVertices().size());
				ImGui::Text("Indices: %d", m.getIndices().size());
				dirtyMaterials = displayMaterial(&m.defaultMaterialIndex, true) || dirtyMaterials;
				ImGui::TreePop();
			}
		}
	}
	ImGui::End();

	if(ImGui::Begin("Node")) {
		if(_selectedNode != entt::null) {
			auto& node = _scene.getRegistry().get<NodeComponent>(_selectedNode);
			ImGui::InputText("Name", &node.name);

			// TEMP Buttons
			if(ImGui::Button("Duplicate")) {
				duplicateSelectedNode();
			}
			ImGui::SameLine();
			if(ImGui::Button("Delete"))
				deleteSelectedNode();
		}
		if(_selectedNode != entt::null) {
			auto& node = _scene.getRegistry().get<NodeComponent>(_selectedNode);
			if(ImGui::TreeNodeEx("Transform Matrix", ImGuiTreeNodeFlags_Leaf)) {
				float matrixTranslation[3], matrixRotation[3], matrixScale[3];
				// ImGui::Matrix("Local Transform", _scene[_selectedNode].transform);
				ImGuizmo::DecomposeMatrixToComponents(reinterpret_cast<float*>(&node.transform), matrixTranslation, matrixRotation, matrixScale);
				updatedTransform = ImGui::InputFloat3("Translation", matrixTranslation) || updatedTransform;
				updatedTransform = ImGui::InputFloat3("Rotation   ", matrixRotation) || updatedTransform;
				updatedTransform = ImGui::InputFloat3("Scale      ", matrixScale) || updatedTransform;
				if(updatedTransform)
					ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, reinterpret_cast<float*>(&node.transform));

				if(ImGui::RadioButton("Translate (T)", _currentGizmoOperation == ImGuizmo::TRANSLATE))
					_currentGizmoOperation = ImGuizmo::TRANSLATE;
				ImGui::SameLine();
				if(ImGui::RadioButton("Rotate (R)", _currentGizmoOperation == ImGuizmo::ROTATE))
					_currentGizmoOperation = ImGuizmo::ROTATE;
				ImGui::SameLine();
				if(ImGui::RadioButton("Scale (Y)", _currentGizmoOperation == ImGuizmo::SCALE))
					_currentGizmoOperation = ImGuizmo::SCALE;

				if(_currentGizmoOperation != ImGuizmo::SCALE) {
					if(ImGui::RadioButton("Local", _currentGizmoMode == ImGuizmo::LOCAL))
						_currentGizmoMode = ImGuizmo::LOCAL;
					ImGui::SameLine();
					if(ImGui::RadioButton("World", _currentGizmoMode == ImGuizmo::WORLD))
						_currentGizmoMode = ImGuizmo::WORLD;
				}

				ImGui::Checkbox("Snap", &_useSnap);
				ImGui::SameLine();
				switch(_currentGizmoOperation) {
					case ImGuizmo::TRANSLATE:
						// snap = config.mSnapTranslation;
						ImGui::InputFloat3("Snap", &_snapOffset.x);
						break;
					case ImGuizmo::ROTATE:
						// snap = config.mSnapRotation;
						ImGui::InputFloat("Angle Snap", &_snapOffset.x);
						break;
					case ImGuizmo::SCALE:
						// snap = config.mSnapScale;
						ImGui::InputFloat("Scale Snap", &_snapOffset.x);
						break;
				}

				ImGui::TreePop();
			}
			if(auto* meshComp = _scene.getRegistry().try_get<MeshRendererComponent>(_selectedNode); meshComp != nullptr) {
				auto& mesh = _scene[meshComp->meshIndex];
				if(ImGui::TreeNodeEx("MeshRenderer", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Text("Mesh: %s", mesh.name.c_str());
					dirtyMaterials = displayMaterial(&meshComp->materialIndex, true) || dirtyMaterials;
					ImGui::TreePop();
				}
			}
			if(auto* meshComp = _scene.getRegistry().try_get<SkinnedMeshRendererComponent>(_selectedNode); meshComp != nullptr) {
				auto& mesh = _scene[meshComp->meshIndex];
				if(ImGui::TreeNodeEx("SkinnedMeshRenderer", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Text("Mesh: %s", mesh.name.c_str());
					ImGui::Text("Skin: %d", meshComp->skinIndex);
					ImGui::Text("BLAS: %d", meshComp->blasIndex);
					ImGui::Text("IndexIntoOffsetTable: %d", meshComp->indexIntoOffsetTable);
					dirtyMaterials = displayMaterial(&meshComp->materialIndex, true) || dirtyMaterials;
					ImGui::TreePop();
				}
			}
			if(auto* animComp = _scene.getRegistry().try_get<AnimationComponent>(_selectedNode); animComp != nullptr) {
				if(ImGui::TreeNodeEx("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Running", &animComp->running);
					ImGui::InputFloat("Time", &animComp->time);
					int anim = animComp->animationIndex;
					if(ImGui::InputInt("Animation Clip", &anim)) {
						if(anim == -1 || (anim >= 0 && anim < Animations.size())) {
							animComp->animationIndex = AnimationIndex(anim);
						}
					}
					ImGui::TreePop();
				}
			}
		} else {
			ImGui::Text("No selected node.");
		}
	}
	ImGui::End();

	// Re-upload materials (FIXME: Can easily be optimised)
	if(dirtyMaterials) {
		vkDeviceWaitIdle(_device); // Overkill
		writeGBufferDescriptorSets();
		uploadMaterials();		// TODO: Optimize by updating only the relevant slice
		recordCommandBuffers(); // FIXME: We're passing metalness and roughness as push constants, so we have to re-record command buffer, this should probably be part
								// of a uniform buffer (like the model matrix?)
	}
	if(ImGui::Begin("Settings")) {
		ImGui::SliderFloat("Time Scale", &_timeScale, 0, 2.0);
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
		if(ImGui::Checkbox("Enable Reflections", &_enableReflections)) {
			_outdatedCommandBuffers = true;
			if(!_enableReflections) {
				VkClearColorValue color{0};
				for(auto& image : _reflectionImages)
					_device.immediateSubmitGraphics([&](const auto& cmdBuff) { vkCmdClearColorImage(cmdBuff, image, VK_IMAGE_LAYOUT_GENERAL, &color, 1, &Image::RangeColorMip0); });
			}
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
			plot("Reflection & Direct Light Filter Time", _reflectionDirectLightFilterTimes);
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
		if(ImPlot::BeginPlot("Updates (CPU)")) {
			ImPlot::SetupAxes("Frame Number", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			plot("Scene Update", _scene.getUpdateTimes());
			plot("BLAS Update", _renderer.getCPUBLASUpdateTimes());
			plot("TLAS Update", _renderer.getCPUTLASUpdateTimes());
			ImPlot::EndPlot();
		}
		if(ImPlot::BeginPlot("Updates (GPU)")) {
			ImPlot::SetupAxes("Frame Number", "Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			plot("Dynamic BLAS", _renderer.getDynamicBLASUpdateTimes());
			plot("TLAS", _renderer.getTLASUpdateTimes());
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

void Editor::cleanupUI() {
	vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	_imguiCommandPool.destroy();
}
