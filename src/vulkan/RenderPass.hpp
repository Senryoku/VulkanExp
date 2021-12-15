#pragma once

#include <stdexcept>

#include "Device.hpp"
#include "HandleWrapper.hpp"

class RenderPass : public HandleWrapper<VkRenderPass> {
  public:
	template<int A, int S, int D>
	void create(VkDevice device, const std::array<VkAttachmentDescription, A>& attachments, const std::array<VkSubpassDescription, S>& subpasses,
				const std::array<VkSubpassDependency, D>& dependencies) {
		VkRenderPassCreateInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = A,
			.pAttachments = attachments.data(),
			.subpassCount = S,
			.pSubpasses = subpasses.data(),
			.dependencyCount = D,
			.pDependencies = dependencies.data(),
		};

		VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &_handle));
		_device = device;
	}
	void create(VkDevice device, const std::vector<VkAttachmentDescription>& attachments, const std::vector<VkSubpassDescription>& subpasses,
				const std::vector<VkSubpassDependency>& dependencies, VkRenderPassCreateFlags flags = 0, const void* next = nullptr) {
		VkRenderPassCreateInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.pNext = next,
			.flags = flags,
			.attachmentCount = static_cast<uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = static_cast<uint32_t>(subpasses.size()),
			.pSubpasses = subpasses.data(),
			.dependencyCount = static_cast<uint32_t>(dependencies.size()),
			.pDependencies = dependencies.data(),
		};

		VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr, &_handle));
		_device = device;
	}

	void create(VkDevice device, VkFormat imageFormat, VkFormat depthFormat) {
		VkAttachmentDescription colorAttachment{
			.format = imageFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		};

		VkAttachmentDescription depthAttachment{
			.format = depthFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference colorAttachmentRef{
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference depthAttachmentRef{
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentRef,
			.pDepthStencilAttachment = &depthAttachmentRef,
		};

		// Wait on start implicit subpass
		VkSubpassDependency dependency{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		};

		std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

		create(device, attachments, std::array<VkSubpassDescription, 1>{subpass}, std::array<VkSubpassDependency, 1>{dependency});
	}

	void destroy() {
		if(isValid()) {
			vkDestroyRenderPass(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

  private:
	VkDevice _device;
};

class RenderPassBuilder {
  public:
	RenderPassBuilder() {
		// Avoid reallocations
		_attachments.reserve(32);
		_attachmentReferences.reserve(32);
		_subpasses.reserve(32);
		_dependencies.reserve(32);
		_preserveAttachments.reserve(32);
	}

	RenderPassBuilder& add(const VkAttachmentDescription& attachment) {
		_attachments.push_back(attachment);
		return *this;
	}

	RenderPassBuilder& addSubPass(VkPipelineBindPoint bindPoint, std::vector<VkAttachmentReference>&& colorAttachments, std::vector<VkAttachmentReference>&& inputAttachment,
								  std::vector<VkAttachmentReference>&& resolveAttachments, VkAttachmentReference&& depthStencilAttachment,
								  std::vector<uint32_t>&& preserveAttachments, VkSubpassDescriptionFlags flags = 0) {
		_attachmentReferences.reserve(_attachmentReferences.size() + 5);

		_attachmentReferences.push_back(std::move(colorAttachments));
		const auto& cA = _attachmentReferences.back();
		_attachmentReferences.push_back(std::move(inputAttachment));
		const auto&							iA = _attachmentReferences.back();
		std::vector<VkAttachmentReference>* resolveA = nullptr;
		if(resolveAttachments.size() > 0) {
			_attachmentReferences.push_back(std::move(resolveAttachments));
			resolveA = &_attachmentReferences.back();
		}
		_attachmentReferences.push_back({depthStencilAttachment});
		VkAttachmentReference* dSA = _attachmentReferences.back().data();
		_preserveAttachments.push_back(std::move(preserveAttachments));
		const auto& pA = _preserveAttachments.back();

		VkSubpassDescription desc{
			.flags = flags,
			.pipelineBindPoint = bindPoint,
			.inputAttachmentCount = static_cast<uint32_t>(iA.size()),
			.pInputAttachments = iA.data(),
			.colorAttachmentCount = static_cast<uint32_t>(cA.size()),
			.pColorAttachments = cA.data(),
			.pResolveAttachments = resolveA ? resolveA->data() : nullptr,
			.pDepthStencilAttachment = dSA,
			.preserveAttachmentCount = static_cast<uint32_t>(pA.size()),
			.pPreserveAttachments = pA.data(),
		};
		_subpasses.push_back(desc);
		return *this;
	}

	RenderPassBuilder& add(const VkSubpassDependency dependency) {
		_dependencies.push_back(dependency);
		return *this;
	}

	[[nodiscard]] RenderPass&& build(const Device& device, VkRenderPassCreateFlags flags = 0) {
		RenderPass r;
		r.create(device, _attachments, _subpasses, _dependencies, flags);
		return std::move(r);
	}

  private:
	std::vector<VkAttachmentDescription>			_attachments;
	std::vector<std::vector<VkAttachmentReference>> _attachmentReferences;
	std::vector<VkSubpassDescription>				_subpasses;
	std::vector<VkSubpassDependency>				_dependencies;
	std::vector<std::vector<uint32_t>>				_preserveAttachments;
};
