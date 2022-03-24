#pragma once

#include <vector>

#include <glm/glm.hpp>

struct SkeletalAnimation {
	uint32_t			   jointsCount;
	std::vector<float>	   times;
	std::vector<glm::mat4> transforms; // times.size() * jointsCount transforms

	float				   length() const { return times.back(); }
	std::vector<glm::mat4> at(float t) {
		// FIXME ?
		if(times.empty()) {
			std::vector<glm::mat4> r;
			for(size_t i = 0; i < jointsCount; ++i)
				r.push_back(glm::mat4(1.0f));
			return r;
		} else if(times.size() == 1) {
			return transforms;
		}

		t = std::fmod(t, length());
		std::vector<glm::mat4> r;
		r.reserve(jointsCount);
		size_t kf = 0;
		while(times[kf] < t)
			++kf;
		float frac = (t - times[kf]) / (times[kf + 1] - times[kf]);
		for(size_t i = 0; i < jointsCount; ++i)
			r.push_back(t * transforms[kf * jointsCount + i] + (1.0f - t) * transforms[(kf + 1) * jointsCount + i]);
		return r;
	}
};
