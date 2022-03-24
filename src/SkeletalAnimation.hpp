#pragma once

#include <vector>

#include <glm/glm.hpp>

struct SkeletalAnimation {
	enum class Interpolation {
		Linear,
		Step,
		CubicSpline
	};
	enum class Path {
		Translation,
		Rotation,
		Scale,
		Weights
	};

	static Interpolation parseInterpolation(const std::string& val) {
		if(val == "LINEAR")
			return Interpolation::Linear;
		if(val == "STEP")
			return Interpolation::Step;
		if(val == "CUBICSPLINE")
			return Interpolation::CubicSpline;
		assert(false);
		return Interpolation::Linear;
	}

	static Path parsePath(const std::string& val) {
		if(val == "translation")
			return Path::Translation;
		if(val == "rotation")
			return Path::Rotation;
		if(val == "scale")
			return Path::Scale;
		if(val == "weights")
			return Path::Weights;
		assert(false);
		return Path::Translation;
	}

	uint32_t jointsCount;

	template<typename T>
	struct Channel {
		Interpolation	   interpolation;
		std::vector<float> times;
		std::vector<T>	   frames;

		void add(float t, const T& d) {
			times.push_back(t);
			frames.push_back(d);
		}
	};
	using TranslationChannel = Channel<glm::vec3>;
	using RotationChannel = Channel<glm::vec4>;
	using ScaleChannel = Channel<glm::vec3>;
	using WeightsChannel = Channel<glm::vec4>;

	TranslationChannel translationKeyFrames;
	RotationChannel	   rotationKeyFrames;
	ScaleChannel	   scaleKeyFrames;
	WeightsChannel	   weightsKeyFrames;

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
