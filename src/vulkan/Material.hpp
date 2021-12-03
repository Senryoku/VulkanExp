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

class Material {
  public:
	struct Texture {
		std::filesystem::path source;
		VkFormat			  format = VK_FORMAT_R8G8B8A8_SRGB;
		JSON::object		  samplerDescription;
		Sampler				  sampler;
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
