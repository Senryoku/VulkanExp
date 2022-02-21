#pragma once

#include <Logger.hpp>

inline std::string toString(VkResult errorCode) {
	switch(errorCode) {
#define GEN_VK_RESULT_STR(r) \
	case VK_##r: return #r
		GEN_VK_RESULT_STR(NOT_READY);
		GEN_VK_RESULT_STR(TIMEOUT);
		GEN_VK_RESULT_STR(EVENT_SET);
		GEN_VK_RESULT_STR(EVENT_RESET);
		GEN_VK_RESULT_STR(INCOMPLETE);
		GEN_VK_RESULT_STR(ERROR_OUT_OF_HOST_MEMORY);
		GEN_VK_RESULT_STR(ERROR_OUT_OF_DEVICE_MEMORY);
		GEN_VK_RESULT_STR(ERROR_OUT_OF_POOL_MEMORY);
		GEN_VK_RESULT_STR(ERROR_FRAGMENTED_POOL);
		GEN_VK_RESULT_STR(ERROR_INITIALIZATION_FAILED);
		GEN_VK_RESULT_STR(ERROR_DEVICE_LOST);
		GEN_VK_RESULT_STR(ERROR_MEMORY_MAP_FAILED);
		GEN_VK_RESULT_STR(ERROR_LAYER_NOT_PRESENT);
		GEN_VK_RESULT_STR(ERROR_EXTENSION_NOT_PRESENT);
		GEN_VK_RESULT_STR(ERROR_FEATURE_NOT_PRESENT);
		GEN_VK_RESULT_STR(ERROR_INCOMPATIBLE_DRIVER);
		GEN_VK_RESULT_STR(ERROR_TOO_MANY_OBJECTS);
		GEN_VK_RESULT_STR(ERROR_FORMAT_NOT_SUPPORTED);
		GEN_VK_RESULT_STR(ERROR_SURFACE_LOST_KHR);
		GEN_VK_RESULT_STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
		GEN_VK_RESULT_STR(SUBOPTIMAL_KHR);
		GEN_VK_RESULT_STR(ERROR_OUT_OF_DATE_KHR);
		GEN_VK_RESULT_STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
		GEN_VK_RESULT_STR(ERROR_VALIDATION_FAILED_EXT);
		GEN_VK_RESULT_STR(ERROR_INVALID_SHADER_NV);
#undef GEN_VK_RESULT_STR
		default: return "UNKNOWN_ERROR";
	}
}

#define VK_CHECK(f)                                                                                                                         \
	{                                                                                                                                       \
		VkResult vulkan_result = (f);                                                                                                       \
		if(vulkan_result != VK_SUCCESS) {                                                                                                   \
			auto error_string = fmt::format("Vulkan Error at {}:{}: Function returned '{}'.", __FILE__, __LINE__, toString(vulkan_result)); \
			error(error_string);                                                                                                            \
			throw std::runtime_error(error_string);                                                                                         \
		}                                                                                                                                   \
	}
