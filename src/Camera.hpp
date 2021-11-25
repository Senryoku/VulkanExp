#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

class Camera {
  public:
	Camera(glm::vec3 position = BasePosition, glm::vec3 direction = BaseDirection, glm::vec3 up = BaseUp, float speed = BaseSpeed, float sensitivity = BaseSensitivity);
	~Camera() = default;

	void strafeRight(float dt = 1.f);
	void strafeLeft(float dt = 1.f);

	void moveForward(float dt = 1.f);
	void moveBackward(float dt = 1.f);
	void moveUp(float dt = 1.f);
	void moveDown(float dt = 1.f);

	void look(glm::vec2 v);

	void updateView();
	void updateProjection(float ratio);

	void reset();

	inline float			getFoV() const { return _fov; }
	inline float			getNear() const { return _near; }
	inline float			getFar() const { return _far; }
	inline const glm::mat4& getViewMatrix() const { return _viewMatrix; }
	inline const glm::mat4& getProjectionMatrix() const { return _projection; }
	inline const glm::mat4& getInvProjection() const { return _invProjection; }
	inline const glm::mat4& getInvViewMatrix() const { return _invViewMatrix; }

	inline void setPosition(const glm::vec3& pos) { _position = pos; }
	inline void setDirection(const glm::vec3& dir) { _direction = dir; }
	inline void lookAt(const glm::vec3& at) { _direction = glm::normalize(at - _position); }

	inline void setFoV(float fov) {
		_fov = fov;
		updateProjection(_ratio);
	}
	inline void setNear(float fnear) {
		_near = fnear;
		updateProjection(_ratio);
	}
	inline void setFar(float ffar) {
		_far = ffar;
		updateProjection(_ratio);
	}

	inline glm::vec3&		getPosition() { return _position; }
	inline const glm::vec3& getPosition() const { return _position; }
	inline const glm::vec3& getDirection() const { return _direction; }
	inline const glm::vec3& getUp() const { return _up; }
	inline const glm::vec3& getRight() const { return _cross; }

	static const glm::vec3 BasePosition;
	static const glm::vec3 BaseDirection;
	static const glm::vec3 BaseUp;
	static const float	   BaseSpeed;
	static const float	   BaseSensitivity;

	// Public attributes
	float speed = BaseSpeed;
	float sensitivity = BaseSensitivity;

  private:
	glm::vec3 _position = BasePosition;
	glm::vec3 _direction = BaseDirection;
	glm::vec3 _up = BaseUp;

	glm::vec3 _cross{1.0f};
	glm::vec2 _moveMouvement{0.0f};

	glm::mat4 _viewMatrix{1.0f};

	// Projection data
	float	  _fov = 60.0f;
	float	  _ratio = 16.0f / 9.0f;
	float	  _near = 0.1f;
	float	  _far = 4000.0f;
	glm::mat4 _projection{1.0f};

	// Cache
	glm::mat4 _invProjection{1.0f};
	glm::mat4 _invViewMatrix{1.0f};
	glm::mat4 _invViewProjection{1.0f};
};
