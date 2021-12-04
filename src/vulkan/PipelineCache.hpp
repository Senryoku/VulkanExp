#pragma once

#include <cassert>
#include <filesystem>
#include <fstream>

#include "HandleWrapper.hpp"

class PipelineCache : public HandleWrapper<VkPipelineCache> {
  public:
	PipelineCache() = default;
	PipelineCache(const PipelineCache&) = delete;
	PipelineCache(PipelineCache&& p) noexcept : HandleWrapper(p._handle), _device(p._device) {
		p._handle = VK_NULL_HANDLE;
		p._device = VK_NULL_HANDLE;
	}
	~PipelineCache() { destroy(); }

	// Create a PipelineCache object on [device] using cached data in file at [path] for initialisation.
	void create(VkDevice device, const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary);
		if(!file)
			throw std::runtime_error(fmt::format("PipelineCache Error: Could not open file '{}' for reading.", path.string()));
		std::streampos fileSize;
		file.seekg(0, std::ios::end);
		fileSize = file.tellg();
		file.seekg(0, std::ios::beg);
		std::vector<char> data;
		data.resize(fileSize);
		file.read(reinterpret_cast<std::ifstream::char_type*>(&data.front()), fileSize);
		create(device, data.size(), data.data());
	}

	void create(VkDevice device, size_t initialDataSize = 0, const void* pInitialData = nullptr, VkPipelineCacheCreateFlags flags = 0) {
		VkPipelineCacheCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
			.pNext = nullptr,
			.flags = flags,
			.initialDataSize = initialDataSize,
			.pInitialData = pInitialData,
		};
		VK_CHECK(vkCreatePipelineCache(device, &info, nullptr, &_handle));
		_device = device;
	}

	void save(const std::filesystem::path& path) {
		assert(isValid());
		size_t size = 0;
		VK_CHECK(vkGetPipelineCacheData(_device, _handle, &size, nullptr));
		char* data = new char[size];
		VK_CHECK(vkGetPipelineCacheData(_device, _handle, &size, data));
		std::ofstream file(path, std::ios::binary);
		if(!file)
			throw std::runtime_error(fmt::format("PipelineCache Error: Could not open file '{}' for writing.", path.string()));
		file.write(data, size);
		delete[] data;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyPipelineCache(_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
			_device = VK_NULL_HANDLE;
		}
	}

  private:
	VkDevice _device = VK_NULL_HANDLE;
};
