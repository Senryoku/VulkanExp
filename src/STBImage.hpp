#pragma once

#include <filesystem>

#include <stb_image.h>

class STBImage {
  public:
	STBImage() = default;
	STBImage(STBImage&&);
	STBImage(const std::filesystem::path& path) { load(path); }
	~STBImage() {
		if(_data) {
			stbi_image_free(_data);
			_data = nullptr;
		}
	}

	void load(const std::filesystem::path& path) {
		_data = stbi_load(path.string().c_str(), &_x, &_y, &_n, STBI_rgb_alpha); // STBI_rgb_alpha: Force 4 channels
		if(!_data) {
			throw std::runtime_error("Image::load Error: Could not load '" + path.string() + "'\n");
		}
	}

	size_t getWidth() const { return _x; }
	size_t getHeight() const { return _y; }
	size_t getChannelCount() const { return _n; }
	size_t byteSize() const { return static_cast<size_t>(_x) * _y * _n; }

	const unsigned char* getData() const { return _data; }

  private:
	unsigned char* _data = nullptr;
	int			   _x = 0, _y = 0, _n = 0;
};
