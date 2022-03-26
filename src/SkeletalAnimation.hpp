#pragma once

#include <vector>

#include <entt/include/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

struct SkeletalAnimationClip {
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

		T at(float t, const T& def = T()) const {
			if(times.empty()) {
				return def;
			} else if(times.size() == 1 || interpolation == Interpolation::Step) {
				size_t i = 0;
				while(i + 1 < times.size() && times[i + 1] < t)
					++i;
				return frames[i];
			}
			t = std::fmod(t, times.back());
			size_t i = 0;
			while(i + 2 < times.size() && times[i + 1] < t)
				++i;
			if(interpolation == Interpolation::Linear) {
				t = (t - times[i]) / (times[i + 1] - times[i]);
				if constexpr(std::is_same<T, glm::quat>::value) {
					return glm::slerp(frames[i], frames[i + 1], t);
				} else {
					// Doesn't compile because it tries to compile it even for glm::quat... MSVC bug?
					// return glm::lerp(frames[i], frames[i + 1], t);
					return glm::mix(frames[i], frames[i + 1], t);
					// return (1.0f - t) * frames[i] + t * frames[i + 1];
				}
			}
			if(interpolation == Interpolation::CubicSpline) {
				// TODO
			}
		}
	};
	using TranslationChannel = Channel<glm::vec3>;
	using RotationChannel = Channel<glm::quat>;
	using ScaleChannel = Channel<glm::vec3>;
	using WeightsChannel = Channel<glm::vec4>;

	struct NodePose {
		glm::mat4 transform;
		glm::vec4 weights;
	};

	struct NodeAnimation {
		entt::entity	   entity;
		TranslationChannel translationKeyFrames;
		RotationChannel	   rotationKeyFrames;
		ScaleChannel	   scaleKeyFrames;
		WeightsChannel	   weightsKeyFrames;

		NodePose at(float t) const {
			auto translate = translationKeyFrames.at(t);
			auto rotation = rotationKeyFrames.at(t);
			auto scale = scaleKeyFrames.at(t, glm::vec3(1.0f));
			auto weights = weightsKeyFrames.at(t);
			return {
				.transform = glm::translate(glm::mat4(1.0f), translate) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale),
				.weights = weights,
			};
		}
	};

	std::unordered_map<entt::entity, NodeAnimation> nodeAnimations;
};
