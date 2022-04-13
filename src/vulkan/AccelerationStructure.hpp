#pragma once

#include "Buffer.hpp"
#include "Device.hpp"

class AccelerationStructure : public HandleWrapper<VkAccelerationStructureKHR> {
  public:
	AccelerationStructure() = default;
	AccelerationStructure(const AccelerationStructure&) = delete;
	AccelerationStructure(AccelerationStructure&& o) noexcept : HandleWrapper(o._handle), _device(o._device) {
		o._handle = VK_NULL_HANDLE;
		o._device = VK_NULL_HANDLE;
	}
	~AccelerationStructure() { destroy(); }

	void create(const Device& device, const Buffer& buffer, size_t offset, size_t size, VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR) {
		VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.pNext = VK_NULL_HANDLE,
			.buffer = buffer,
			.offset = offset,
			.size = size,
			.type = type,
		};
		VK_CHECK(vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo, nullptr, &_handle));

		_device = &device;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyAccelerationStructureKHR(*_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
		}
	}

	inline [[nodiscard]] VkDeviceAddress getDeviceAddress() const {
		VkAccelerationStructureDeviceAddressInfoKHR BLASAddressInfo{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
			.accelerationStructure = _handle,
		};
		return vkGetAccelerationStructureDeviceAddressKHR(*_device, &BLASAddressInfo);
	}

	inline static [[nodiscard]] VkAccelerationStructureBuildSizesInfoKHR getBuildSize(const Device&										 device,
																					  const VkAccelerationStructureBuildGeometryInfoKHR& accelerationBuildGeometryInfo,
																					  const uint32_t									 primitiveCount) {
		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &accelerationBuildGeometryInfo, &primitiveCount,
												&accelerationStructureBuildSizesInfo);
		return accelerationStructureBuildSizesInfo;
	}

  private:
	const Device* _device = nullptr;
};
