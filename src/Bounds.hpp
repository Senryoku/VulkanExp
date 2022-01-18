#pragma once

#include <array>
#include <glm/glm.hpp>

struct Bounds {
	glm::vec3 min;
	glm::vec3 max;

	inline Bounds& operator+=(const Bounds& o) {
		min = glm::min(min, o.min);
		max = glm::max(max, o.max);
		return *this;
	}

	inline Bounds operator+(const Bounds& o) {
		return {
			.min = glm::min(min, o.min),
			.max = glm::max(max, o.max),
		};
	}

	inline bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

	inline std::array<glm::vec3, 8> getPoints() const {
		return {min,
				glm::vec3{min.x, min.y, max.z},
				glm::vec3{min.x, max.y, min.z},
				glm::vec3{min.x, max.y, max.z},
				glm::vec3{max.x, min.y, min.z},
				glm::vec3{max.x, min.y, max.z},
				glm::vec3{max.x, max.y, min.z},
				max};
	}
};

inline Bounds operator*(const glm::mat4& transform, const Bounds& b) {
	return {
		.min = glm::vec3(transform * glm::vec4(b.min, 1.0f)),
		.max = glm::vec3(transform * glm::vec4(b.max, 1.0f)),
	};
}
