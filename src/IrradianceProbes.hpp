#pragma once

#include <DescriptorPool.hpp>
#include <DescriptorSetLayout.hpp>
#include <Image.hpp>
#include <Pipeline.hpp>

#include <glTF.hpp>

struct ShaderBindingTable {
	Buffer		 buffer;
	DeviceMemory memory;

	VkStridedDeviceAddressRegionKHR raygenEntry;
	VkStridedDeviceAddressRegionKHR missEntry;
	VkStridedDeviceAddressRegionKHR anyhitEntry;
	VkStridedDeviceAddressRegionKHR callableEntry;

	void destroy() {
		buffer.destroy();
		memory.free();
	}
};

class IrradianceProbes {
  public:
	void init(const Device& device, uint32_t familyQueueIndex, glm::vec3 min, glm::vec3 max);
	void createShaderBindingTable();
	void writeDescriptorSet(const glTF& scene, VkAccelerationStructureKHR tlas);
	void update(const glTF& scene);

	const size_t ColorResolution = 8;
	const size_t DepthResolution = 16;
	const size_t VolumeResolution[3]{32, 8, 32};

	inline const glm::vec3	getMin() const { return _min; }
	inline const glm::vec3	getMax() const { return _max; }
	inline const Image&		getColor() const { return _color; }
	inline const Image&		getDepth() const { return _depth; }
	inline const ImageView& getColorView() const { return _colorView; }
	inline const ImageView& getDepthView() const { return _depthView; }

	void destroy();

  private:
	const Device* _device;

	Pipeline			_pipeline;
	PipelineLayout		_pipelineLayout;
	DescriptorSetLayout _descriptorSetLayout;
	DescriptorPool		_descriptorPool;
	CommandPool			_commandPool;
	CommandBuffers		_commandBuffers;

	ShaderBindingTable _shaderBindingTable;

	Image	  _color;
	ImageView _colorView;
	Image	  _depth;
	ImageView _depthView;

	glm::vec3 _min;
	glm::vec3 _max;
};
