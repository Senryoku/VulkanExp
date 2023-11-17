#pragma once

#include <stdexcept>
#include <vector>

#include "CommandBuffer.hpp"
#include "HandleWrapper.hpp"
#include "RenderPass.hpp"
#include "Vertex.hpp"

class PipelineLayout : public HandleWrapper<VkPipelineLayout> {
  public:
	void create(VkDevice device, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {}, const std::vector<VkPushConstantRange> pushConstants = {}) {
		VkPipelineLayoutCreateInfo pipelineLayoutInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
			.pSetLayouts = descriptorSetLayouts.size() > 0 ? descriptorSetLayouts.data() : nullptr,
			.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size()),
			.pPushConstantRanges = pushConstants.size() > 0 ? pushConstants.data() : nullptr,
		};
		create(device, pipelineLayoutInfo);
	}

	void create(VkDevice device, const VkPipelineLayoutCreateInfo& info) {
		VK_CHECK(vkCreatePipelineLayout(device, &info, nullptr, &_handle));
		_device = device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyPipelineLayout(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~PipelineLayout() { destroy(); }

  private:
	VkDevice _device = VK_NULL_HANDLE;
};

class Pipeline : public HandleWrapper<VkPipeline> {
  public:
	void create(VkDevice device, const VkGraphicsPipelineCreateInfo& info, VkPipelineCache pipelineCache = VK_NULL_HANDLE) {
		assert(!isValid());
		VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &info, nullptr, &_handle));
		_device = device;
	}

	void create(VkDevice device, const VkRayTracingPipelineCreateInfoKHR& info, VkPipelineCache pipelineCache = VK_NULL_HANDLE) {
		assert(!isValid());
		VK_CHECK(vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, pipelineCache, 1, &info, nullptr, &_handle));
		_device = device;
	}

	void create(VkDevice device, const VkComputePipelineCreateInfo& info, VkPipelineCache pipelineCache = VK_NULL_HANDLE) {
		assert(!isValid());
		VK_CHECK(vkCreateComputePipelines(device, pipelineCache, 1, &info, nullptr, &_handle));
		_device = device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyPipeline(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
		_pipelineLayout.destroy();
	}

	void bind(const CommandBuffer& commandBuffer, VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS) const { vkCmdBindPipeline(commandBuffer, bindPoint, _handle); }

	const PipelineLayout& getLayout() const { return _pipelineLayout; }
	PipelineLayout&		  getLayout() { return _pipelineLayout; }

	~Pipeline() { destroy(); }

  private:
	VkDevice	   _device = VK_NULL_HANDLE;
	PipelineLayout _pipelineLayout;
};
