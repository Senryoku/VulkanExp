#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VkTools.hpp"

template<typename T>
class HandleWrapper {
  public:
	HandleWrapper& operator=(HandleWrapper&& o) noexcept {
		_handle = o._handle;
		o._handle = VK_NULL_HANDLE;
		return *this;
	}
	HandleWrapper& operator=(const HandleWrapper&) = default;
	explicit HandleWrapper(T handle) : _handle(handle) {}
	// T		 getHandle() const { return _handle; }
	const T& getHandle() const { return _handle; }
			 operator T() const { return _handle; }
	bool	 isValid() const { return _handle != VK_NULL_HANDLE; }

  protected:
	HandleWrapper() = default;
	HandleWrapper(const HandleWrapper& hw) = default; // NOTE: Should we delete this by default?
	HandleWrapper(HandleWrapper&& hw) noexcept : _handle(hw._handle) { hw._handle = VK_NULL_HANDLE; }
	T _handle = VK_NULL_HANDLE;
};
