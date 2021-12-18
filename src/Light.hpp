#pragma once

#include <glm/glm.hpp>

struct LightBuffer {
	glm::vec4 direction{1.0, -2.0, 1.0, 1.0};
	glm::vec4 color{1.0, 1.0, 1.0, 1.0};
};
