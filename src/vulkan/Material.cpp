#include "Material.hpp"

// FIXME: Move this (Resource Managment class/singleton?)
std::unordered_map<std::string, GPUImage> Images;

VkFilter glTFToVkFilter(int e) {
	switch(e) {
		case 9728: return VK_FILTER_NEAREST;
		case 9729: return VK_FILTER_LINEAR;
		case 9987: return VK_FILTER_LINEAR; // LINEAR_MIPMAP_LINEAR
	}
	assert(false);
}

VkSamplerAddressMode glTFtoVkSamplerAddressMode(int e) {
	switch(e) {
		case 33071: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case 33648: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case 10497: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
	assert(false);
}

void Material::uploadTextures(const Device& device, uint32_t queueFamilyIndex) {
	for(auto& tex : textures) {
		auto  path = tex.second.source.lexically_normal().string();
		auto& texR = tex.second;
		if(!Images.contains(path)) {
			STBImage image{texR.source.c_str()};
			Images.try_emplace(path);
			Images[path].image.setDevice(device);
			Images[path].image.upload(image, queueFamilyIndex);
			Images[path].imageView.create(device, Images[path].image, VK_FORMAT_R8G8B8A8_SRGB);
			VkPhysicalDeviceProperties properties{};
			vkGetPhysicalDeviceProperties(device.getPhysicalDevice(), &properties);
			VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			if(texR.samplerDescription.contains("wrapU")) // FIXME: Doesn't actually exist?
				addressModeW = glTFtoVkSamplerAddressMode(texR.samplerDescription["wrapU"].as<int>());
			texR.sampler.create(device, VkSamplerCreateInfo{
											.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
											.magFilter = glTFToVkFilter(texR.samplerDescription["magFilter"].as<int>()),
											.minFilter = glTFToVkFilter(texR.samplerDescription["minFilter"].as<int>()),
											.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
											.addressModeU = glTFtoVkSamplerAddressMode(texR.samplerDescription["wrapS"].as<int>()),
											.addressModeV = glTFtoVkSamplerAddressMode(texR.samplerDescription["wrapT"].as<int>()),
											.addressModeW = addressModeW,
											.mipLodBias = 0.0f,
											.anisotropyEnable = VK_TRUE,
											.maxAnisotropy = properties.limits.maxSamplerAnisotropy,
											.compareEnable = VK_FALSE,
											.compareOp = VK_COMPARE_OP_ALWAYS,
											.minLod = 0.0f,
											.maxLod = static_cast<float>(Images[path].image.getMipLevels()),
											.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
											.unnormalizedCoordinates = VK_FALSE,
										});
			texR.gpuImage = &Images[path];
		}
	}
}
