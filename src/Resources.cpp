#include "Resources.hpp"

#include <QuickTimer.hpp>
#include <ThreadPool.hpp>
#include <array>

VkFilter glTFToVkFilter(int e) {
	switch(e) {
		case 9728: return VK_FILTER_NEAREST;
		case 9729: return VK_FILTER_LINEAR;
		case 9984: return VK_FILTER_NEAREST; // NEAREST_MIPMAP_NEAREST
		case 9985: return VK_FILTER_NEAREST; // LINEAR_MIPMAP_NEAREST
		case 9986: return VK_FILTER_NEAREST; // NEAREST_MIPMAP_LINEAR
		case 9987: return VK_FILTER_LINEAR;	 // LINEAR_MIPMAP_LINEAR
	}
	assert(false);
	return VK_FILTER_LINEAR;
}

VkSamplerMipmapMode glTFToVkSamplerMipmapMode(int e) {
	switch(e) {
		case 9728: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
		case 9729: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
		case 9984: return VK_SAMPLER_MIPMAP_MODE_NEAREST; // NEAREST_MIPMAP_NEAREST
		case 9985: return VK_SAMPLER_MIPMAP_MODE_NEAREST; // LINEAR_MIPMAP_NEAREST
		case 9986: return VK_SAMPLER_MIPMAP_MODE_LINEAR;  // NEAREST_MIPMAP_LINEAR
		case 9987: return VK_SAMPLER_MIPMAP_MODE_LINEAR;  // LINEAR_MIPMAP_LINEAR
	}
	assert(false);
	return VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

VkSamplerAddressMode glTFtoVkSamplerAddressMode(int e) {
	switch(e) {
		case 33071: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case 33648: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case 10497: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
	assert(false);
	return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

void uploadTextures(const Device& device, uint32_t queueFamilyIndex) {
	std::unordered_map<std::string, STBImage> images;
	std::mutex								  imagesMutex;
	std::vector<std::future<void>>			  wait;
	{
		QuickTimer qt("Loading textures from disk");
		for(auto& tex : Textures) {
			auto path = tex.source.lexically_normal().string();
			if(!Images.contains(path)) {
				wait.emplace_back(ThreadPool::GetInstance().queue([&, path] {
					STBImage					 image{tex.source.c_str()};
					std::unique_lock<std::mutex> lock(imagesMutex);
					images[path] = std::move(image);
				}));
			}
		}
		for(auto& f : wait)
			f.wait();
	}
	QuickTimer qt("Upload textures to device");
	for(auto& tex : Textures) {
		auto  path = tex.source.lexically_normal().string();
		auto& texR = tex;

		if(!Images.contains(path)) {
			Images.try_emplace(path);
			Images[path].image.setDevice(device);
			Images[path].image.upload(images[path], queueFamilyIndex, texR.format);
			Images[path].imageView.create(device, Images[path].image, texR.format);
		}
		texR.gpuImage = &Images[path];
		auto magFilter = glTFToVkFilter(texR.samplerDescription["magFilter"].as<int>(9729));
		auto minFilter = glTFToVkFilter(texR.samplerDescription["minFilter"].as<int>(9729));
		auto mipMapMode = glTFToVkSamplerMipmapMode(texR.samplerDescription["minFilter"].as<int>(9729));
		auto wrapS = glTFtoVkSamplerAddressMode(texR.samplerDescription["wrapS"].as<int>(10497));
		auto wrapT = glTFtoVkSamplerAddressMode(texR.samplerDescription["wrapT"].as<int>(10497));
		texR.sampler = getSampler(device, magFilter, minFilter, mipMapMode, wrapS, wrapT, Images[path].image.getMipLevels());
	}
}

Sampler* getSampler(const Device& device, VkFilter magFilter, VkFilter minFilter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode wrapS, VkSamplerAddressMode wrapT,
					float maxLod) {
	// 4 bits per properties should be enough (Unless we count Vulkan extensions... Eh.)
	size_t key = magFilter | (minFilter << 4) | (wrapS << 8) | (wrapT << 12) | (mipmapMode << 16) || (static_cast<size_t>(maxLod) << 32);
	if(!Samplers.contains(key)) {
		Samplers.try_emplace(key);
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(device.getPhysicalDevice(), &properties);
		Samplers[key].create(device, VkSamplerCreateInfo{
										 .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
										 .magFilter = magFilter,
										 .minFilter = minFilter,
										 .mipmapMode = mipmapMode,
										 .addressModeU = wrapS,
										 .addressModeV = wrapT,
										 .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
										 .mipLodBias = 0.0f,
										 .anisotropyEnable = VK_TRUE,
										 .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
										 .compareEnable = VK_FALSE,
										 .compareOp = VK_COMPARE_OP_ALWAYS,
										 .minLod = 0.0f,
										 .maxLod = maxLod,
										 .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
										 .unnormalizedCoordinates = VK_FALSE,
									 });
	}
	return &Samplers[key];
}
