#pragma once

#include <Bounds.hpp>

struct Ray {
	glm::vec3 origin;
	glm::vec3 direction;

	inline glm::vec3 operator()(float depth) const { return origin + depth * direction; }
};

struct Hit {
	bool  hit = false;
	float depth = std::numeric_limits<float>::max();
};

inline Hit intersect(const Ray& r, const Bounds& b) {
	const glm::vec3 dir_inv = glm::vec3(1.0) / r.direction;
	float			t1 = (b.min[0] - r.origin[0]) * dir_inv[0];
	float			t2 = (b.max[0] - r.origin[0]) * dir_inv[0];

	float tmin = glm::min(t1, t2);
	float tmax = glm::max(t1, t2);

	for(int i = 1; i < 3; ++i) {
		t1 = (b.min[i] - r.origin[i]) * dir_inv[i];
		t2 = (b.max[i] - r.origin[i]) * dir_inv[i];

		tmin = glm::max(tmin, glm::min(t1, t2));
		tmax = glm::min(tmax, glm::max(t1, t2));
	}
	// TODO: Handle being inside the bounds.
	return {.hit = tmax >= glm::max(tmin, 0.0f), .depth = tmin > 0 ? tmin : tmax};
}
