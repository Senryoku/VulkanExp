#pragma once

#include <vector>
#include <fstream>

#include "HandleWrapper.hpp"

class Shader : public HandleWrapper<VkShaderModule> {
public:
	bool load(VkDevice device, const std::string& path) {
		auto source = readFile(path);
		VkShaderModuleCreateInfo createInfo{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = source.size(),
			.pCode = reinterpret_cast<const uint32_t*>(source.data()),
		};
		if (vkCreateShaderModule(device, &createInfo, nullptr, &_handle) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create shader module!");
			return false;
		}
		_device = device;
		return true;
	}

	void destroy() {
		if (isValid()) {
			vkDestroyShaderModule(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	~Shader() {
		destroy();
	}
private:
	VkDevice _device;

	static std::vector<char> readFile(const std::string& filename) {
		std::ifstream file(filename, std::ios::ate | std::ios::binary);

		if (!file.is_open()) {
			throw std::runtime_error(fmt::format("Failed to open file '{}'.", filename));
		}
		size_t fileSize = (size_t)file.tellg();
		std::vector<char> buffer(fileSize);
		file.seekg(0);
		file.read(buffer.data(), fileSize);
		file.close();

		return buffer;
	}
};