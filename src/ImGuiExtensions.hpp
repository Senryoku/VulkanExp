#pragma once

#include <glm/glm.hpp>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

namespace ImGui {

inline void Matrix(const char* name, const glm::mat4& m) {
	for(uint32_t i = 0; i < 4; ++i)
		Text("%9.2e %9.2e %9.2e %9.2e", m[i][0], m[i][1], m[i][2], m[i][3]);
}

} // namespace ImGui
