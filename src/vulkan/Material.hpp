#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Image.hpp"
#include "ImageView.hpp"
#include "Sampler.hpp"

#include <JSON.hpp>

#include "DescriptorSetLayout.hpp"
#include "Device.hpp"

struct GPUImage {
	Image	  image;
	ImageView imageView;
};
// FIXME: Move this (Resource Managment class/singleton?)
inline std::unordered_map<std::string, GPUImage> Images;
inline std::unordered_map<size_t, Sampler>		 Samplers;

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

class Material {
  public:
	struct Texture {
		std::filesystem::path source;
		VkFormat			  format = VK_FORMAT_R8G8B8A8_SRGB;
		JSON::object		  samplerDescription;
		Sampler*			  sampler = nullptr;
		GPUImage*			  gpuImage = nullptr;
	};

	std::unordered_map<std::string, Texture> textures;

	std::string name;
	glm::vec4	baseColorFactor{1.0, 1.0, 1.0, 1.0};
	float		metallicFactor = 1.0;
	float		roughnessFactor = 1.0;

	void uploadTextures(const Device& device, uint32_t queueFamilyIndex);

  private:
};
