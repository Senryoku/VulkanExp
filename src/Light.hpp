#pragma once

#include <glm/glm.hpp>

struct LightBuffer {
	glm::vec4 direction = {glm::normalize(glm::vec3{0.2, 2.0, 0.2}), 1.0};
	glm::vec4 color{10.0f};
};
