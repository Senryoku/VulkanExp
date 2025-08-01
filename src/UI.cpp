﻿#include "Editor.hpp"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <IconsFontAwesome6.h>
#include <ImGuiExtensions.hpp>
#include <ImGuizmo.h>
#include <implot/implot.h>
#include <misc/cpp/imgui_stdlib.h>
#include "imgui_internal.h"
#include <voxels/VoxelTerrain.hpp>

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
	io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;	// Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
	// io.ConfigViewportsNoAutoMerge = true;
	// io.ConfigViewportsNoTaskBarIcon = true;

	const float font_size = 16.0f;
	io.Fonts->AddFontFromFileTTF("ext/imgui-docking/misc/fonts/DroidSans.ttf", font_size);
	static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
	ImFontConfig		 icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	io.Fonts->AddFontFromFileTTF("data/fonts/fontawesome/otfs/Font Awesome 6 Free-Solid-900.otf", font_size, &icons_config, icons_ranges);

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

	ImGui_ImplVulkan_CreateFontsTexture();

	for(TextureIndex i = TextureIndex{0}; i < TextureIndex(Textures.size()); ++i) {
		SceneUITextureIDs.push_back({i, ImGui_ImplVulkan_AddTexture(Textures[i].sampler->getHandle(), Textures[i].gpuImage->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	}

	const auto sampler = getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0)->getHandle();

	ProbesRayIrradianceDepth = ImGui_ImplVulkan_AddTexture(sampler, _irradianceProbes.getRayIrradianceDepthView(), VK_IMAGE_LAYOUT_GENERAL);
	ProbesRayDirection = ImGui_ImplVulkan_AddTexture(sampler, _irradianceProbes.getRayDirectionView(), VK_IMAGE_LAYOUT_GENERAL);
	ProbesColor = ImGui_ImplVulkan_AddTexture(sampler, _irradianceProbes.getIrradianceView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	ProbesDepth = ImGui_ImplVulkan_AddTexture(sampler, _irradianceProbes.getDepthView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void tooltip(const char* str) {
	if(ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::Text(str);
		ImGui::EndTooltip();
	}
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

	const auto sampler = getSampler(_device, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0)->getHandle();

	DebugTextureIDs.clear();
	for(size_t i = 0; i < _reflectionImageViews.size(); ++i)
		DebugTextureIDs.push_back({fmt::format("Reflection {}", i), ImGui_ImplVulkan_AddTexture(sampler, _reflectionImageViews[i], VK_IMAGE_LAYOUT_GENERAL)});
	for(size_t i = 0; i < _directLightImageViews.size(); ++i)
		DebugTextureIDs.push_back({fmt::format("Direct Light {}", i), ImGui_ImplVulkan_AddTexture(sampler, _directLightImageViews[i], VK_IMAGE_LAYOUT_GENERAL)});
	for(size_t i = 0; i < _gbufferImageViews.size(); ++i)
		DebugTextureIDs.push_back(
			{fmt::format("GBuffer {}", i),
			 ImGui_ImplVulkan_AddTexture(sampler, _gbufferImageViews[i], (i % _gbufferSize == 4) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)});
	for(size_t i = 0; i < _reflectionIntermediateFilterImageViews.size(); ++i)
		DebugTextureIDs.push_back(
			{fmt::format("Reflection Filtered {}", i), ImGui_ImplVulkan_AddTexture(sampler, _reflectionIntermediateFilterImageViews[i], VK_IMAGE_LAYOUT_GENERAL)});
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

	ImVec2 menuSize;
	if(ImGui::BeginMainMenuBar()) {
		menuSize = ImGui::GetWindowSize();
		if(ImGui::BeginMenu("File")) {
			if(ImGui::MenuItem("Load Scene")) {
				_scene.load("data/default.scene");
			}
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Edit")) {
			if(ImGui::MenuItem("Undo", "CTRL+Z", false, _history.canUndo()))
				_history.undo();
			if(ImGui::MenuItem("Redo", "CTRL+Y", false, _history.canRedo()))
				_history.redo();
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
			_irradianceProbes.createPipeline(_pipelineCache);
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

	bool updatedTransform = false;

	// On screen gizmos
	{
		// Dummy Window for "on field" widgets
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2{static_cast<float>(_width), static_cast<float>(_height)});
		ImGui::SetNextWindowBgAlpha(0.0);
		ImGui::Begin("SelectedObject", nullptr,
					 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
						 ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImDrawList* drawlist = ImGui::GetWindowDrawList();
		auto		winpos = ImGui::GetMainViewport()->Pos;
		glm::vec2	glmwinpos{winpos.x, winpos.y};
		auto		project = [&](const glm::mat4& transform, const glm::vec3& pos = glm::vec3(0)) {
			   auto t = _camera.getViewMatrix() * transform * glm::vec4(pos, 1.0);
			   if(t.z > 0.0) // Truncate if point is behind camera
				   t.z = 0.0;
			   t = _camera.getProjectionMatrix() * t;
			   auto r = glm::vec2{t.x, -t.y} / t.w;
			   r = 0.5f * (r + 1.0f);
			   r.x *= _width;
			   r.y *= _height;
			   return r + glmwinpos;
		};

		ImGuiIO& io = ImGui::GetIO();
		int		 winx, winy;
		glfwGetWindowPos(_window, &winx, &winy);
		ImGuizmo::SetRect(winx, winy, io.DisplaySize.x, io.DisplaySize.y);
		switch(_controlMode) {
			case ControlMode::Node: {
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
						for(int i = 0; i < 24; i += 2)
							drawlist->AddLine(screen_aabb[segments[i]], screen_aabb[segments[i + 1]], ImGui::ColorConvertFloat4ToU32(ImVec4(0.0, 0.0, 1.0, 0.5)));
					}
					// Display Bones & Joints
					auto* animationComponent = _scene.getRegistry().try_get<AnimationComponent>(_selectedNode);
					if(skinnedMesh || animationComponent) {
						std::vector<entt::entity> joints;
						if(skinnedMesh && skinnedMesh->skinIndex != InvalidSkinIndex)
							joints = _scene.getSkins()[skinnedMesh->skinIndex].joints;
						else if(animationComponent && animationComponent->animationIndex != InvalidAnimationIndex)
							for(const auto& na : Animations[animationComponent->animationIndex].nodeAnimations)
								joints.push_back(na.first);
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
								drawlist->AddLine(ImVec2(parentPixel), ImVec2(childPixel), ImGui::ColorConvertFloat4ToU32(ImVec4(0.0, 1.0, 0.0, 0.5)));
								child = childNode.next;
							}
						}
					}
					glm::mat4 delta;
					bool	  gizmoUpdated =
						ImGuizmo::Manipulate(reinterpret_cast<const float*>(&_camera.getViewMatrix()), reinterpret_cast<const float*>(&_camera.getProjectionMatrix()),
											 _currentGizmoOperation, _currentGizmoMode, reinterpret_cast<float*>(&worldTransform), reinterpret_cast<float*>(&delta),
											 _useSnap ? (_currentGizmoOperation == ImGuizmo::OPERATION::ROTATE	? &_snapAngleOffset
														 : _currentGizmoOperation == ImGuizmo::OPERATION::SCALE ? &_snapScaleOffset
																												: &_snapOffset.x)
													  : nullptr);
					// Keep track of the starting point of the modification
					static bool		 updatingGizmo = false;
					static glm::mat4 startingTransform;
					if(gizmoUpdated) {
						if(!updatingGizmo) {
							updatingGizmo = true;
							startingTransform = node.transform;
						}
						node.transform = glm::inverse(parentTransform) * worldTransform;
					} else if(updatingGizmo) { // Finalize the transform modification
						updatingGizmo = false;
						_history.push(new NodeTransformModification(_scene, _selectedNode, startingTransform, node.transform));
					}
					updatedTransform = gizmoUpdated || updatedTransform;
				}

				if(_selectedNode != entt::null && updatedTransform) {
					_scene.markDirty(_selectedNode);
				}
				break;
			}
			case ControlMode::Voxel: {
				// TODO: Display current target?
				auto r = getMouseRay();
				auto terrains = _scene.getRegistry().view<NodeComponent, VoxelTerrain>();
				terrains.each([&](const auto entity, auto& node, auto& terrain) {
					Hit			 best;
					entt::entity bestNode = entt::null;
					auto		 bounds = Bounds(glm::vec3(0), glm::vec3(Chunk::Size * VoxelTerrain::Size));
					bounds = node.globalTransform * bounds;
					if(auto hit = intersect(r, bounds); hit.hit) {
						auto child = node.first;
						while(child != entt::null) {
							const auto& childNode = _scene.getRegistry().get<NodeComponent>(child);
							Ray			localRay = glm::inverse(childNode.globalTransform) * r;
							hit = intersect(localRay, _scene[_scene.getRegistry().get<MeshRendererComponent>(child).meshIndex]);
							hit.depth = glm::length(glm::vec3(childNode.globalTransform * glm::vec4((localRay.origin + localRay.direction * hit.depth), 1.0f)) - r.origin);
							if(hit.hit && hit.depth < best.depth) {
								best = hit;
								bestNode = entity;
							}
							child = childNode.next;
						}
					}
					if(best.hit) {
						auto pos = glm::inverse(node.globalTransform) * glm::vec4(r(best.depth - 0.0001), 1.0);
						pos += glm::vec4(0.5);
						pos = glm::trunc(pos);
						// FIXME: Not quite right
						auto mat = glm::translate(glm::mat4(1.0), glm::vec3(pos));
						// FIXME: Transparent ?
						ImGuizmo::DrawCubes(&_camera.getViewMatrix()[0][0], &_camera.getProjectionMatrix()[0][0], &mat[0][0], 1);
					}
				});
				break;
			}
		}
		ImGui::End(); // Dummy Window
	}

	//////////////////////////////////////////////////////////////////////////

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos + ImVec2(_width / 2, menuSize.y), ImGuiCond_Always, ImVec2(0.5, 0));
	ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
	if(ImGui::Begin("##ContextualActions", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove)) {
		switch(_controlMode) {
			case ControlMode::Node: {
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0);
				auto toggleButton = [&](const char* icon, bool active) {
					if(active) {
						ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
						ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 4.0);
					}
					bool ret = ImGui::Button(icon);
					if(active) {
						ImGui::PopStyleColor();
						ImGui::PopStyleVar(1);
					}
					return ret;
				};
				auto modeButton = [&](const char* icon, ImGuizmo::OPERATION op, const char* tooltipStr) {
					const bool active = _currentGizmoOperation == op;
					if(toggleButton(icon, active))
						_currentGizmoOperation = op;
					tooltip(tooltipStr);
					ImGui::SameLine();
				};

				modeButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, ImGuizmo::TRANSLATE, "Translate (T)");
				modeButton(ICON_FA_ARROWS_ROTATE, ImGuizmo::ROTATE, "Rotate (R)");
				modeButton(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, ImGuizmo::SCALE, "Scale (Y)");

				if(_currentGizmoOperation == ImGuizmo::SCALE)
					ImGui::BeginDisabled();
				if(_currentGizmoMode == ImGuizmo::LOCAL) {
					if(ImGui::Button("Local", ImVec2(50, 0)))
						_currentGizmoMode = ImGuizmo::WORLD;
				} else {
					if(ImGui::Button("World", ImVec2(50, 0)))
						_currentGizmoMode = ImGuizmo::LOCAL;
					tooltip("Toggle between Local and World transform edition mode.");
				}
				if(_currentGizmoOperation == ImGuizmo::SCALE)
					ImGui::EndDisabled();
				ImGui::SameLine();

				if(toggleButton(ICON_FA_ARROWS_DOWN_TO_LINE, _useSnap))
					_useSnap = !_useSnap;
				tooltip("Snap transform to the specified value.");
				ImGui::SameLine();
				ImGui::PushItemWidth(120);
				switch(_currentGizmoOperation) {
					case ImGuizmo::TRANSLATE: ImGui::InputFloat3("##Snap Offset", &_snapOffset.x); break;
					case ImGuizmo::ROTATE: ImGui::InputFloat(reinterpret_cast<const char*>(u8"°##Angle Snap"), &_snapAngleOffset); break;
					case ImGuizmo::SCALE: ImGui::InputFloat("##Scale Snap", &_snapScaleOffset); break;
				}
				tooltip("Snap Value");
				ImGui::PopItemWidth();

				ImGui::PopStyleVar();
				break;
			}
			case ControlMode::Voxel: {
				ImGui::Text("Voxel");
				break;
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::PopStyleVar();

	//////////////////////////////////////////////////////////////////////////

	if(ImGui::Begin("Animation")) {
		const static ImVec4 axisColors[3]{
			ImVec4(1, 0, 0, 1),
			ImVec4(0, 0, 1, 1),
			ImVec4(0, 1, 0, 1),
		};

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
				if(ImGui::Button(animComp->running ? "Pause" : "Play") || ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
					animComp->running = !animComp->running;
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
					if(ImGui::BeginTabBar("#Track", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
						auto&	   na = anim.nodeAnimations[selectedAnimationNode];
						const auto duration = na.duration();
						if(ImGui::BeginTabItem("Position")) {
							// TODO
							ImGui::Text("TODO");
							ImGui::EndTabItem();
						}
						if(ImGui::BeginTabItem("Rotation")) {
							if(na.rotationKeyFrames.times.size() > 0) {
								float rot_duration = na.rotationKeyFrames.duration();
								if(ImGui::InputFloat("Duration (s)", &rot_duration))
									na.rotationKeyFrames.setDuration(rot_duration);
								ImPlotAxisFlags common_ax_flags = ImPlotAxisFlags_Lock;
								if(ImPlot::BeginSubplots("##Rotation", 3, 1, ImVec2(-1, 360),
														 ImPlotSubplotFlags_NoLegend | ImPlotSubplotFlags_LinkRows | ImPlotSubplotFlags_ShareItems)) {
									static const char* rotationSubplotsNames[] = {"##RotationX", "##RotationY", "##RotationZ"};
									ImPlotFlags		   plotFlags = ImPlotFlags_CanvasOnly;
									for(size_t c = 0; c < 3; ++c) {
										if(ImPlot::BeginPlot(rotationSubplotsNames[c], ImVec2(-1, 0), plotFlags)) {
											static std::vector<ImPlotPoint> points;
											points.clear();
											ImPlot::SetupAxes(0, 0, common_ax_flags, common_ax_flags);
											ImPlot::SetupAxesLimits(0, duration, -180, 180, ImPlotCond_Always);

											if(ImPlot::IsPlotHovered() && ImGui::IsMouseDown(0)) {
												auto pos = ImPlot::GetPlotMousePos();
												animComp->running = false;
												animComp->forceUpdate = true;
												animComp->time = pos.x;
											}

											if(ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(1)) {
												auto	  pos = ImPlot::GetPlotMousePos();
												glm::vec3 value{0};
												value[c] = pos.y / (360.0f / (2.0f * glm::pi<float>()));
												na.rotationKeyFrames.add(pos.x, value);
											}

											double time = std::fmod(animComp->time, duration);
											ImPlot::SetNextLineStyle(ImVec4(1, 1, 1, 1));
											ImPlot::PlotInfLines("Time", &time, 1);
											ImPlot::TagX(time, ImVec4(1, 1, 1, 1));
											int toDelete = -1;
											for(size_t i = 0; i < na.rotationKeyFrames.times.size(); ++i) {
												auto		euler = glm::eulerAngles(na.rotationKeyFrames.frames[i]); // FIXME: Instable
												ImPlotPoint point{na.rotationKeyFrames.times[i], 360.0f / (2.0f * glm::pi<float>()) * euler[c]};
												if(ImPlot::DragPoint(c * na.rotationKeyFrames.times.size() + i, &point.x, &point.y, axisColors[c], 4)) {
													if(ImGui::IsKeyPressed(ImGuiKey_Delete)) {
														toDelete = i;
													} else {
														point.x = glm::clamp(
															point.x, static_cast<double>(i > 0 ? na.rotationKeyFrames.times[i - 1] : 0),
															static_cast<double>(i < na.rotationKeyFrames.times.size() - 1 ? na.rotationKeyFrames.times[i + 1] : duration));
														euler[c] = point.y / (360.0f / (2.0f * glm::pi<float>()));
														na.rotationKeyFrames.times[i] = point.x;
														na.rotationKeyFrames.frames[i] = glm::quat(euler);

														animComp->running = false;
														animComp->forceUpdate = true;
														animComp->time = na.rotationKeyFrames.times[i];
													}
												}
												// ImPlot::Annotation(point.x, point.y, ImVec4(1, 1, 1, 1), ImVec2(0, 6), false, "%f
												// %f", point.x, point.y);
												points.push_back(point);
											}
											if(toDelete >= 0)
												na.rotationKeyFrames.del(toDelete);
											ImPlot::SetNextLineStyle(axisColors[c]);
											ImPlot::PlotLine("##h1", &points[0].x, &points[0].y, points.size(), 0, sizeof(ImPlotPoint));
											ImPlot::EndPlot();
										}
									}
								}
								ImPlot::EndSubplots();
							}
							ImGui::EndTabItem();
						}
						if(ImGui::BeginTabItem("Scale")) {
							// TODO
							ImGui::Text("TODO");
							ImGui::EndTabItem();
						}
						if(ImGui::BeginTabItem("Weight")) {
							// TODO
							ImGui::Text("TODO");
							ImGui::EndTabItem();
						}
						ImGui::EndTabBar();
					}
				}
			}
		}
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
		for(int i = 0; i < _gbufferSize; ++i)
			quickDisplay(_reflectionImageViews.size() + _directLightImageViews.size() + _gbufferSize * _currentFrame + i);

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
					if(droppedEntity != entity) {
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
			if(ImGui::Button("Duplicate"))
				duplicateSelectedNode();
			ImGui::SameLine();
			if(ImGui::Button(ICON_FA_TRASH_CAN " Delete"))
				deleteSelectedNode();
		}
		if(_selectedNode != entt::null) {
			auto& node = _scene.getRegistry().get<NodeComponent>(_selectedNode);
			if(ImGui::TreeNodeEx("Transform Matrix", ImGuiTreeNodeFlags_Leaf)) {
				float matrixTranslation[3], matrixRotation[3], matrixScale[3];
				ImGuizmo::DecomposeMatrixToComponents(reinterpret_cast<float*>(&node.transform), matrixTranslation, matrixRotation, matrixScale);
				updatedTransform = ImGui::InputFloat3("Translation", matrixTranslation) || updatedTransform;
				updatedTransform = ImGui::InputFloat3("Rotation   ", matrixRotation) || updatedTransform;
				updatedTransform = ImGui::InputFloat3("Scale      ", matrixScale) || updatedTransform;
				if(updatedTransform) {
					auto prevTransform = node.transform;
					ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, reinterpret_cast<float*>(&node.transform));
					_history.push(new NodeTransformModification(_scene, _selectedNode, prevTransform, node.transform));
					_scene.markDirty(_selectedNode);
				}
				ImGui::TreePop();
			}

			std::vector<const char*> absentComponentTypes;

			auto displayMesh = [&](MeshIndex meshIndex) {
				if(meshIndex != InvalidMeshIndex) {
					auto& mesh = _scene[meshIndex];
					ImGui::Text("Mesh: %s (%d)", mesh.name.c_str(), meshIndex);
				} else
					ImGui::Text("No Mesh Assigned");
			};

			if(auto* meshComp = _scene.getRegistry().try_get<MeshRendererComponent>(_selectedNode); meshComp != nullptr) {
				if(ImGui::TreeNodeEx("MeshRenderer", ImGuiTreeNodeFlags_DefaultOpen)) {
					displayMesh(meshComp->meshIndex);
					dirtyMaterials = displayMaterial(&meshComp->materialIndex, true) || dirtyMaterials;
					ImGui::TreePop();
				}
			} else
				absentComponentTypes.push_back("MeshRenderer");
			if(auto* meshComp = _scene.getRegistry().try_get<SkinnedMeshRendererComponent>(_selectedNode); meshComp != nullptr) {
				if(ImGui::TreeNodeEx("SkinnedMeshRenderer", ImGuiTreeNodeFlags_DefaultOpen)) {
					displayMesh(meshComp->meshIndex);
					ImGui::Text("Skin: %d", meshComp->skinIndex);
					ImGui::Text("BLAS: %d", meshComp->blasIndex);
					ImGui::Text("IndexIntoOffsetTable: %d", meshComp->indexIntoOffsetTable);
					dirtyMaterials = displayMaterial(&meshComp->materialIndex, true) || dirtyMaterials;
					ImGui::TreePop();
				}
			} else
				absentComponentTypes.push_back("SkinnedMeshRenderer");
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
			} else
				absentComponentTypes.push_back("Animation");

			// Add Component
			ImGui::Dummy(ImVec2(0.0f, 20.0f));
			ImGui::Separator();
			ImGui::Dummy(ImVec2(0.0f, 20.0f));
			static char componentType[256] = "";
			if(ImGui::BeginCombo("##ComponentType", componentType)) {
				for(const auto& type : absentComponentTypes) {
					bool is_selected = strcmp(type, componentType) == 0;
					if(ImGui::Selectable(type, is_selected))
						strcpy_s(componentType, type);
					if(is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if(ImGui::Button("Add Component")) {
				if(strcmp(componentType, "MeshRenderer") == 0)
					_scene.getRegistry().emplace<MeshRendererComponent>(_selectedNode);
				else if(strcmp(componentType, "SkinnedMeshRenderer") == 0)
					_scene.getRegistry().emplace<SkinnedMeshRendererComponent>(_selectedNode);
				else if(strcmp(componentType, "Animation") == 0)
					_scene.getRegistry().emplace<AnimationComponent>(_selectedNode);
				strcpy_s(componentType, "");
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
			ImGui::PushItemWidth(80);
			if(ImGui::DragFloat("Near Plane", &fnear, 1.f, 0.f, 100.f))
				_camera.setNear(fnear);
			ImGui::SameLine();
			float ffar = _camera.getFar();
			if(ImGui::DragFloat("Far Plane", &ffar, 1.f, 100.f, 40000.f))
				_camera.setFar(ffar);
			ImGui::TreePop();
			ImGui::PopItemWidth();
		}
		if(ImGui::TreeNodeEx("Light & Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Use time of day", &_deriveLightPositionFromTime);
			if(!_deriveLightPositionFromTime)
				ImGui::BeginDisabled();
			ImGui::InputFloat("Day Cycle Speed", &_dayCycleSpeed, 0.0f, 100.f);
			ImGui::InputInt("Day of the Year", &_dayOfTheYear, 0, 365);

			ImGui::PushItemWidth(50);
			ImGui::InputInt("##Hour", &_hour, 0, 24);
			ImGui::SameLine();
			ImGui::Text(":");
			ImGui::SameLine();
			ImGui::InputFloat("##Minute", &_minute, 0.0f, 60.0f);
			ImGui::SameLine();
			ImGui::Text("Time");

			ImGui::InputFloat("##Longitude", &_longitude, 0.0f, 90.f);
			ImGui::SameLine();
			ImGui::Text((const char*)u8"° / ");
			ImGui::SameLine();
			ImGui::InputFloat("##Latitude", &_latitude, 0.0f, 90.0f);
			ImGui::SameLine();
			ImGui::Text((const char*)u8"° Long./Lat.");

			ImGui::PopItemWidth();

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
			ImGui::SameLine();
			if(ImGui::Button("Update State")) {
				_irradianceProbes.initProbes(_computeQueue);
			}
			ImGui::SameLine();
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
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);
	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	_imguiCommandPool.destroy();
}
