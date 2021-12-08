#pragma once

#include <Image.hpp>

#include <glTF.hpp>

class IrradianceProbes {
  public:
	void init(const Device& device, uint32_t familyQueueIndex, glm::vec3 min, glm::vec3 max);

	void update(const glTF& scene);

	const size_t ColorResolution = 8;
	const size_t DepthResolution = 16;

	inline const glm::vec3 getMin() const { return _min; }
	inline const glm::vec3 getMax() const { return _max; }
	inline const Image&	   getColor() const { return _color; }
	inline const Image&	   getDepth() const { return _depth; }

	void destroy();

  private:
	const Device* _device;

	Image	  _color;
	ImageView _colorView;
	Image	  _depth;
	ImageView _depthView;

	glm::vec3 _min;
	glm::vec3 _max;
};
