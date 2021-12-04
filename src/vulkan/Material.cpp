#include "Material.hpp"

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

void Material::uploadTextures(const Device& device, uint32_t queueFamilyIndex) {
	for(auto& tex : textures) {
		auto  path = tex.second.source.lexically_normal().string();
		auto& texR = tex.second;
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
