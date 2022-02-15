#pragma once

#include <Device.hpp>
#include <Image.hpp>
#include <ImageView.hpp>
#include <Sampler.hpp>

#include <JSON.hpp>
#include <TaggedType.hpp>

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

inline std::vector<Texture> Textures;
struct TextureIndexTag {};
using TextureIndex = TaggedIndex<size_t, TextureIndexTag>;
inline const TextureIndex						 InvalidTextureIndex{static_cast<uint32_t>(-1)};
inline std::unordered_map<std::string, GPUImage> Images;
inline std::unordered_map<size_t, Sampler>		 Samplers;
inline Buffer									 MaterialBuffer;
inline DeviceMemory								 MaterialMemory;

void	 uploadTextures(const Device& device, VkQueue queue, const CommandPool& commandPool, const Buffer& stagingBuffer);
Sampler* getSampler(const Device& device, VkFilter magFilter, VkFilter minFilter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode wrapS, VkSamplerAddressMode wrapT,
					float maxLod);
