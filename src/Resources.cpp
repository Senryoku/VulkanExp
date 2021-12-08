#include "Resources.hpp"

#include <array>

VkFilter glTFToVkFilter(int e) {
	switch(e) {
		case 9728: return VK_FILTER_NEAREST;
		case 9729: return VK_FILTER_LINEAR;
		case 9987: return VK_FILTER_LINEAR; // LINEAR_MIPMAP_LINEAR
	}
	assert(false);
	return VK_FILTER_LINEAR;
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
	for(auto& tex : Textures) {
		auto  path = tex.source.lexically_normal().string();
		auto& texR = tex;
		if(!Images.contains(path)) {
			STBImage image{texR.source.c_str()};
			Images.try_emplace(path);
			Images[path].image.setDevice(device);
			Images[path].image.upload(image, queueFamilyIndex, texR.format);
			Images[path].imageView.create(device, Images[path].image, texR.format);
		}
		texR.gpuImage = &Images[path];
		auto magFilter = glTFToVkFilter(texR.samplerDescription["magFilter"].as<int>(9729));
		auto minFilter = glTFToVkFilter(texR.samplerDescription["minFilter"].as<int>(9729));
		auto wrapS = glTFtoVkSamplerAddressMode(texR.samplerDescription["wrapS"].as<int>(10497));
		auto wrapT = glTFtoVkSamplerAddressMode(texR.samplerDescription["wrapT"].as<int>(10497));
		texR.sampler = getSampler(device, magFilter, minFilter, wrapS, wrapT, Images[path].image.getMipLevels());
	}
}

inline Sampler* getSampler(const Device& device, VkFilter magFilter, VkFilter minFilter, VkSamplerAddressMode wrapS, VkSamplerAddressMode wrapT, float maxLod) {
	size_t key = magFilter | (minFilter << 8) | (wrapS << 16) | (wrapT << 24) | (static_cast<size_t>(maxLod) << 32);
	if(!Samplers.contains(key)) {
		Samplers.try_emplace(key);
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(device.getPhysicalDevice(), &properties);
		Samplers[key].create(device, VkSamplerCreateInfo{
										 .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
										 .magFilter = magFilter,
										 .minFilter = minFilter,
										 .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
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