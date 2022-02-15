#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <JSON.hpp>

inline JSON::array toJSON(const glm::vec2& v) {
	return JSON::array{v.x, v.y};
}

inline JSON::array toJSON(const glm::vec3& v) {
	return JSON::array{v.x, v.y, v.z};
}

inline JSON::array toJSON(const glm::vec4& v) {
	return JSON::array{v.x, v.y, v.z, v.w};
}

inline JSON::array toJSON(const glm::quat& v) {
	return JSON::array{v.x, v.y, v.z, v.z};
}

inline JSON::array toJSON(const glm::mat4& m) {
	return JSON::array{
		m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2], m[1][3], m[2][0], m[2][1], m[2][2], m[2][3], m[3][0], m[3][1], m[3][2], m[3][3],
	};
}

template<>
inline glm::vec3 JSON::value::to<glm::vec3>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 3);
	const auto& a = _value.as_array;
	return glm::vec3{a[0].to<float>(), a[1].to<float>(), a[2].to<float>()};
}

template<>
inline glm::vec4 JSON::value::to<glm::vec4>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 4);
	const auto& a = _value.as_array;
	return glm::vec4{
		a[0].to<float>(),
		a[1].to<float>(),
		a[2].to<float>(),
		a[3].to<float>(),
	};
}

template<>
inline glm::quat JSON::value::to<glm::quat>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 4);
	const auto& a = _value.as_array;
	// glm::quat constructor takes w as the first argument.
	return glm::quat{
		a[3].to<float>(),
		a[0].to<float>(),
		a[1].to<float>(),
		a[2].to<float>(),
	};
}

template<>
inline glm::mat4 JSON::value::to<glm::mat4>() const {
	assert(_type == Type::array);
	assert(_value.as_array.size() == 16);
	const auto& a = _value.as_array;
	return glm::mat4{
		a[0].to<float>(), a[1].to<float>(), a[2].to<float>(),  a[3].to<float>(),  a[4].to<float>(),	 a[5].to<float>(),	a[6].to<float>(),  a[7].to<float>(),
		a[8].to<float>(), a[9].to<float>(), a[10].to<float>(), a[11].to<float>(), a[12].to<float>(), a[13].to<float>(), a[14].to<float>(), a[15].to<float>(),
	};
}
