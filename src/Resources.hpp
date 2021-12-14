#pragma once

#include <Device.hpp>
#include <Image.hpp>
#include <ImageView.hpp>
#include <Sampler.hpp>

#include <JSON.hpp>

/*
 * TODO: Clean this up.
 */

struct GPUImage {
	Image	  image;
	ImageView imageView;
};

struct Texture {
	std::filesystem::path source;
	VkFormat			  format = VK_FORMAT_R8G8B8A8_SRGB;
	JSON::object		  samplerDescription;
	Sampler*			  sampler = nullptr;
	GPUImage*			  gpuImage = nullptr;
};

inline std::vector<Texture>						 Textures;
inline std::unordered_map<std::string, GPUImage> Images;
inline std::unordered_map<size_t, Sampler>		 Samplers;
inline Buffer									 MaterialBuffer;
inline DeviceMemory								 MaterialMemory;

void	 uploadTextures(const Device& device, uint32_t queueFamilyIndex);
Sampler* getSampler(const Device& device, VkFilter magFilter, VkFilter minFilter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode wrapS, VkSamplerAddressMode wrapT,
					float maxLod);
