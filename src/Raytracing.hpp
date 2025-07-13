#pragma once

#include <Bounds.hpp>

struct Ray {
	glm::vec3 origin;
	glm::vec3 direction;

	inline glm::vec3 operator()(float depth) const { return origin + depth * direction; }
};

inline Ray operator*(const glm::mat4& transform, const Ray& r) {
	return {
		.origin = glm::vec3(transform * glm::vec4(r.origin, 1.0f)),
		.direction = glm::normalize(glm::mat3(transform) * r.direction),
	};
}

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

#include <Mesh.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/intersect.hpp>

inline Hit intersect(const Ray& r, const Mesh& m) {
	Hit hit;
	if(intersect(r, m.getBounds()).hit)
		for(size_t i = 0; i < m.getIndices().size(); i += 3) {
			glm::vec2		 bary{0.0f};
			float			 distance;
			const glm::vec3& v0 = m.getVertices()[m.getIndices()[i]].pos;
			const glm::vec3& v1 = m.getVertices()[m.getIndices()[i + 1]].pos;
			const glm::vec3& v2 = m.getVertices()[m.getIndices()[i + 2]].pos;
			if(glm::intersectRayTriangle(r.origin, r.direction, v0, v1, v2, bary, distance)) {
				if(distance > 0 && distance < hit.depth) {
					hit.hit = true;
					hit.depth = distance;
				}
			}
		}
	return hit;
}
