#include "Camera.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp> // glm::lookAt
#include <glm/gtc/type_ptr.hpp>			// glm::value_ptr

const glm::vec3 Camera::BasePosition = {-1.f, 1.f, -1.f};
const glm::vec3 Camera::BaseDirection = {1.f, 0.f, 1.f};
const glm::vec3 Camera::BaseUp = {0.f, 1.f, 0.f};
const float		Camera::BaseSpeed = 100.f;
const float		Camera::BaseSensitivity = 0.1f;

Camera::Camera(glm::vec3 position, glm::vec3 direction, glm::vec3 up, float _speed, float _sensitivity)
	: speed(_speed), sensitivity(_sensitivity), _position(position), _direction(direction), _up(up) {
	_cross = glm::normalize(glm::cross(_direction, _up));
}

void Camera::strafeRight(float dt) {
	_position += dt * speed * _cross;
}

void Camera::strafeLeft(float dt) {
	_position -= dt * speed * _cross;
}

void Camera::moveForward(float dt) {
	_position += dt * speed * _direction;
}

void Camera::moveBackward(float dt) {
	_position -= dt * speed * _direction;
}

void Camera::moveUp(float dt) {
	_position += dt * speed * _up;
}

void Camera::moveDown(float dt) {
	_position -= dt * speed * _up;
}

void Camera::look(glm::vec2 v) {
	const auto pi = 3.14159265359f;
	_moveMouvement += v * sensitivity;
	if(_moveMouvement.y > 89)
		_moveMouvement.y = 89;
	else if(_moveMouvement.y < -89)
		_moveMouvement.y = -89;
	double r_temp = std::cos(_moveMouvement.y * pi / 180.);
	_direction.x += r_temp * std::cos(_moveMouvement.x * pi / 180.);
	_direction.y += std::sin(_moveMouvement.y * pi / 180.);
	_direction.z += r_temp * std::sin(_moveMouvement.x * pi / 180.);

	_direction = glm::normalize(_direction);
	_cross = glm::normalize(glm::cross(_direction, _up));
}

void Camera::reset() {
	speed = BaseSpeed;
	sensitivity = BaseSensitivity;
	_position = BasePosition;
	_direction = BaseDirection;
	_up = BaseUp;
}

void Camera::updateView() {
	_viewMatrix = glm::lookAt(_position, _position + _direction, _up);
	_invViewMatrix = glm::inverse(_viewMatrix);
	_invViewProjection = _invViewMatrix * _invProjection;
}

void Camera::updateProjection(float ratio) {
	_ratio = ratio;
	float inRad = _fov * glm::pi<float>() / 180.f;
	_projection = glm::perspective(inRad, _ratio, _near, _far);
	_invProjection = glm::inverse(_projection);
	_invViewProjection = _invViewMatrix * _invProjection;
}
