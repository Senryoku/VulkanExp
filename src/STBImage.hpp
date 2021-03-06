#pragma once

#include <cassert>
#include <filesystem>

#include <stb_image.h>

#include <Logger.hpp>

class STBImage {
  public:
	STBImage() = default;
	STBImage(STBImage&&) noexcept;
	STBImage& operator=(STBImage&&) noexcept;
	STBImage(const std::filesystem::path& path) { load(path); }
	~STBImage() {
		if(_data) {
			stbi_image_free(_data);
			_data = nullptr;
			_x = _y = _n = 0;
		}
	}

	void load(const std::filesystem::path& path) {
		_data = stbi_load(path.string().c_str(), &_x, &_y, &_n, 4); // Force 4 channels
		_n = 4;														// Adjust _n so it reflects the actual _data (we force 4 channels)
		if(!_data)
			error("STBImage::load Error: Could not load '{}'\n", path.string());
	}

	inline size_t getWidth() const { return _x; }
	inline size_t getHeight() const { return _y; }
	inline size_t getChannelCount() const { return _n; }
	inline size_t byteSize() const { return static_cast<size_t>(_x) * _y * _n; }

	const unsigned char* getData() const { return _data; }

  private:
	unsigned char* _data = nullptr;
	int			   _x = 0, _y = 0, _n = 0;
};
