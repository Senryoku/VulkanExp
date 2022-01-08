#pragma once

#include <DescriptorPool.hpp>
#include <DescriptorSetLayout.hpp>
#include <Fence.hpp>
#include <Image.hpp>
#include <Pipeline.hpp>

#include <RollingBuffer.hpp>
#include <Scene.hpp>
#include <ShaderBindingTable.hpp>
#include <vulkan/Query.hpp>

class IrradianceProbes {
  public:
	void init(const Device& device, uint32_t transfertFamilyQueueIndex, uint32_t computeFamilyQueueIndex, glm::vec3 min, glm::vec3 max);
	void initProbes(VkQueue queue);
	void createPipeline();
	void createShaderBindingTable();
	void writeDescriptorSet(const Scene& scene, VkAccelerationStructureKHR tlas, const Buffer& lightBuffer);
	void updateUniforms();
	void update(const Scene& scene, VkQueue queue);

	inline const Image&		getColor() const { return _color; }
	inline const Image&		getDepth() const { return _depth; }
	inline const ImageView& getColorView() const { return _colorView; }
	inline const ImageView& getDepthView() const { return _depthView; }
	inline const Buffer&	getGridParametersBuffer() const { return _gridInfoBuffer; }
	inline const Buffer&	getProbeInfoBuffer() const { return _probeInfoBuffer; }

	void destroy();

	float TargetHysteresis = 0.98f;

	// This will be passed to shaders as a UBO, alignment and order of members is important.
	struct GridInfo {
		glm::vec3	 extentMin;
		float		 depthSharpness = 12.0f; // Exponent for depth testing
		glm::vec3	 extentMax;
		float		 hysteresis = 0.0f; // Importance of previously cast rays, starts low to accelerate probes convergence, will converge towards TargetHysteresis
		glm::ivec3	 resolution{32, 16, 32};
		unsigned int raysPerProbe = 128;
		unsigned int colorRes = 8;
		unsigned int depthRes = 16;
		float		 shadowBias = 0.3f;
		unsigned int layerPerUpdate = 2; // Should be a divisor of resolution.y
	};
	GridInfo GridParameters;

	// This could also hold an offset later on.
	struct ProbeInfo {
		uint32_t state;
	};

	const RollingBuffer<float>& getComputeTimes() const { return _computeTimes; }

	void setLightBuffer(const Buffer& lightBuffer);

  private:
	const Device* _device = nullptr;
	const Buffer* _lightBuffer = nullptr; // As it may change every frame (also used in main render and tied to the number of image in the swapchain), keep a reference to it to
										  // write it right before updates

	Fence				_fence;
	Pipeline			_pipeline;
	PipelineLayout		_pipelineLayout;
	DescriptorSetLayout _descriptorSetLayout;
	DescriptorPool		_descriptorPool;
	CommandPool			_commandPool;
	CommandBuffers		_commandBuffers;
	Pipeline			_pipelineProbeInit;
	ShaderBindingTable	_probeInitShaderBindingTable;

	Buffer			   _gridInfoBuffer;
	DeviceMemory	   _gridInfoMemory;
	Buffer			   _probeInfoBuffer;
	DeviceMemory	   _probeInfoMemory;
	ShaderBindingTable _shaderBindingTable;

	// Exposed results
	Image	  _color;
	ImageView _colorView;
	Image	  _depth;
	ImageView _depthView;

	// Working buffers
	Image	  _workColor;
	ImageView _workColorView;
	Image	  _workDepth;
	ImageView _workDepthView;

	QueryPool			 _queryPool;
	RollingBuffer<float> _computeTimes;

	void writeLightDescriptor();
};
