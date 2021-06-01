#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

template<typename T>
class HandleWrapper {
public:
	HandleWrapper() = default;
	explicit HandleWrapper(T handle) : _handle(handle) { }
	operator T() const { return _handle; }
protected:
	T _handle = VK_NULL_HANDLE;
};