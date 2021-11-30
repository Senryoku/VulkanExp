#pragma once

#include <stdexcept>

#include "HandleWrapper.hpp"

class DescriptorSetLayout : public HandleWrapper<VkDescriptorSetLayout> {
  public:
	void create(VkDevice device);
	void create(VkDevice device, const VkDescriptorSetLayoutCreateInfo& info);
	void destroy();

	~DescriptorSetLayout() { destroy(); }

  private:
	VkDevice _device;
};
