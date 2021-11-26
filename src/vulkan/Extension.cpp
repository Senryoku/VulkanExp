#include "Extension.hpp"

inline PFN_vkCreateAccelerationStructureKHR			  F_vkCreateAccelerationStructureKHR = nullptr;
inline PFN_vkGetAccelerationStructureBuildSizesKHR	  F_vkGetAccelerationStructureBuildSizesKHR = nullptr;
inline PFN_vkGetBufferDeviceAddressKHR				  F_vkGetBufferDeviceAddressKHR = nullptr;
inline PFN_vkGetAccelerationStructureDeviceAddressKHR F_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
inline PFN_vkCmdBuildAccelerationStructuresKHR		  F_vkCmdBuildAccelerationStructuresKHR = nullptr;

VkResult vkCreateAccelerationStructureKHR(VkDevice device, const VkAccelerationStructureCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator,
										  VkAccelerationStructureKHR* pAccelerationStructure) {
	return F_vkCreateAccelerationStructureKHR(device, pCreateInfo, pAllocator, pAccelerationStructure);
}

void vkGetAccelerationStructureBuildSizesKHR(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
											 const uint32_t* pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo) {
	F_vkGetAccelerationStructureBuildSizesKHR(device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo);
}

VkDeviceAddress vkGetBufferDeviceAddressKHR(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
	return F_vkGetBufferDeviceAddressKHR(device, pInfo);
}

VkDeviceAddress vkGetAccelerationStructureDeviceAddressKHR(VkDevice device, const VkAccelerationStructureDeviceAddressInfoKHR* pInfo) {
	return F_vkGetAccelerationStructureDeviceAddressKHR(device, pInfo);
}

void vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
										 const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos) {
	F_vkCmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
}

void loadExtensions(VkInstance instance) {
	// vkGetInstanceProcAddr
}

void loadExtensions(VkDevice device) {
	F_vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
	F_vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");
	F_vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR");
	F_vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");
	F_vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");
}
