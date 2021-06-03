#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

template<typename T>
class HandleWrapper {
public:
	HandleWrapper() = default;
	HandleWrapper(const HandleWrapper& hw) = default;
	HandleWrapper(HandleWrapper&& hw) : _handle(hw._handle) {
		hw._handle = VK_NULL_HANDLE;
	}
	HandleWrapper& operator=(HandleWrapper&& o) noexcept {
		_handle = o._handle;
		o._handle = VK_NULL_HANDLE;
		return *this;
	}
	HandleWrapper& operator=(const HandleWrapper&) = default;
	explicit HandleWrapper(T handle) : _handle(handle) { }
	T getHandle() const { return _handle; }
	operator T() const { return _handle; }
	bool isValid() const { return _handle != VK_NULL_HANDLE; }
protected:
	T _handle = VK_NULL_HANDLE;
};