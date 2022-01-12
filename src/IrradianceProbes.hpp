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

	inline const Image&		getRayIrradianceDepth() const { return _rayIrradianceDepth; }
	inline const ImageView& getRayIrradianceDepthView() const { return _rayIrradianceDepthView; }
	inline const Image&		getRayDirection() const { return _rayDirection; }
	inline const ImageView& getRayDirectionView() const { return _rayDirectionView; }

	inline const Image&		getIrradiance() const { return _irradiance; }
	inline const Image&		getDepth() const { return _depth; }
	inline const ImageView& getIrradianceView() const { return _irradianceView; }
	inline const ImageView& getDepthView() const { return _depthView; }
	inline const Buffer&	getGridParametersBuffer() const { return _gridInfoBuffer; }
	inline const Buffer&	getProbeInfoBuffer() const { return _probeInfoBuffer; }

	void destroy();

	static const uint32_t MaxRaysPerProbe = 256;

	float TargetHysteresis = 0.98f;

	// This will be passed to shaders as a UBO, alignment and order of members is important.
	struct GridInfo {
		glm::vec3		   extentMin;
		float			   depthSharpness = 12.0f; // Exponent for depth testing
		glm::vec3		   extentMax;
		float			   hysteresis = 0.0f; // Importance of previously cast rays, starts low to accelerate probes convergence, will converge towards TargetHysteresis
		glm::ivec3		   resolution{32, 16, 32};
		unsigned int	   raysPerProbe = 192;
		const unsigned int colorRes = 8; // These resolutions are actually baked in the shaders
		const unsigned int depthRes = 16;
		float			   shadowBias = 0.3f;
		unsigned int	   layersPerUpdate = 16; // Should be a divisor of resolution.y
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
	Pipeline			_updateIrradiancePipeline;
	Pipeline			_updateDepthPipeline;
	Pipeline			_copyBordersPipeline;
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
	Image	  _irradiance;
	ImageView _irradianceView;
	Image	  _depth;
	ImageView _depthView;

	// Working buffers
	Image	  _rayIrradianceDepth;
	ImageView _rayIrradianceDepthView;
	Image	  _rayDirection;
	ImageView _rayDirectionView;

	Image	  _workIrradiance;
	ImageView _workIrradianceView;
	Image	  _workDepth;
	ImageView _workDepthView;

	QueryPool			 _queryPool;
	RollingBuffer<float> _computeTimes;

	void writeLightDescriptor();
};
