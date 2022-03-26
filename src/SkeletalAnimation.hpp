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
			auto translate = at(translationKeyFrames, t);
			auto rotation = at(rotationKeyFrames, t);
			auto scale = at(scaleKeyFrames, t, glm::vec3(1.0f));
			auto weights = at(weightsKeyFrames, t);
			return {
				.transform = glm::translate(glm::mat4(1.0f), translate) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale),
				.weights = weights,
			};
		}

		template<typename T>
		T at(const Channel<T> chan, float t, T def = T()) const {
			if(chan.times.empty()) {
				return def;
			} else if(chan.times.size() == 1 || chan.interpolation == Interpolation::Step) {
				size_t i = 0;
				while(i + 1 < chan.times.size() && chan.times[i + 1] < t)
					++i;
				return chan.frames[i];
			}
			t = std::fmod(t, chan.times.back());
			size_t i = 0;
			while(i + 2 < chan.times.size() && chan.times[i + 1] < t)
				++i;
			if(chan.interpolation == Interpolation::Linear) {
				t = (t - chan.times[i]) / (chan.times[i + 1] - chan.times[i]);
				if constexpr(std::is_same<T, glm::quat>()) {
					return glm::slerp(chan.frames[i], chan.frames[i + 1], t);
				} else {
					return (1.0f - t) * chan.frames[i] + t * chan.frames[i + 1];
				}
			}
			if(chan.interpolation == Interpolation::CubicSpline) {
				// TODO
			}
		}
	};

	std::unordered_map<entt::entity, NodeAnimation> nodeAnimations;
};
