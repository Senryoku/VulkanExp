#pragma once

#include <DescriptorPool.hpp>
#include <DescriptorSetLayout.hpp>
#include <Fence.hpp>
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
	void createPipeline();
	void createShaderBindingTable();
	void writeDescriptorSet(const glTF& scene, VkAccelerationStructureKHR tlas);
	void updateUniforms();
	void update(const glTF& scene, VkQueue queue);

	inline const Image&		getColor() const { return _color; }
	inline const Image&		getDepth() const { return _depth; }
	inline const ImageView& getColorView() const { return _colorView; }
	inline const ImageView& getDepthView() const { return _depthView; }

	void destroy();

	// This wii be passed to shaders as a UBO, alignment and order of members is important.
	struct GridInfo {
		glm::vec3	 extentMin;
		float		 depthSharpness = 50.0f; // Exponent for depth testing
		glm::vec3	 extentMax;
		float		 hysteresis = 0.98f; // Importance of newly cast rays
		glm::ivec3	 resolution{32, 8, 32};
		unsigned int raysPerProbe = 16;
		unsigned int colorRes = 8;
		unsigned int depthRes = 16;
		unsigned int padding[2];
	};
	GridInfo GridParameters;

  private:
	const Device* _device;

	Fence				_fence;
	Pipeline			_pipeline;
	PipelineLayout		_pipelineLayout;
	DescriptorSetLayout _descriptorSetLayout;
	DescriptorPool		_descriptorPool;
	CommandPool			_commandPool;
	CommandBuffers		_commandBuffers;

	Buffer			   _gridInfoBuffer;
	DeviceMemory	   _gridInfoMemory;
	ShaderBindingTable _shaderBindingTable;

	// TODO: To allow sampling of probes during probes update, we'll probably need to double these and swap them as input/output each update.
	Image	  _color;
	ImageView _colorView;
	Image	  _depth;
	ImageView _depthView;
};
