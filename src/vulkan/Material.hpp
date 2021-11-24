#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "Image.hpp"
#include "ImageView.hpp"
#include "Sampler.hpp"

#include <JSON.hpp>

struct GPUImage {
	Image	  image;
	ImageView imageView;
};

class Material {
  public:
	struct Texture {
		std::filesystem::path source;
		JSON::object		  samplerDescription;
		Sampler				  sampler;
		GPUImage*			  gpuImage = nullptr;
	};

	std::unordered_map<std::string, Texture> textures;

	void uploadTextures(const Device& device, uint32_t queueFamilyIndex);

  private:
};
