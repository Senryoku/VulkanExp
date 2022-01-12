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
	void destroyPipeline();
	void createShaderBindingTable();
	void writeDescriptorSet(const Scene& scene, VkAccelerationStructureKHR tlas, const Buffer& lightBuffer);
	void updateUniforms();
	void update(const Scene& scene, VkQueue queue);
	void destroy();

	inline uint32_t getProbeCount() const { return GridParameters.resolution.x * GridParameters.resolution.y * GridParameters.resolution.z; }

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

	static const uint32_t MaxRaysPerProbe = 256;

	uint32_t ProbesPerUpdate = 0; // 0 means as much as necessary/possible. Not used yet.

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
		unsigned int	   padding[1];
	};
	GridInfo GridParameters;

	// This could also hold an offset later on.
	struct ProbeInfo {
		uint32_t state;
	};

	const RollingBuffer<float>& getComputeTimes() const { return _computeTimes; }
	const RollingBuffer<float>& getTraceTimes() const { return _traceTimes; }
	const RollingBuffer<float>& getUpdateTimes() const { return _updateTimes; }
	const RollingBuffer<float>& getBorderCopyTimes() const { return _borderCopyTimes; }
	const RollingBuffer<float>& getCopyTimes() const { return _copyTimes; }

	void setLightBuffer(const Buffer& lightBuffer);

  private:
	const Device* _device = nullptr;
	const Buffer* _lightBuffer = nullptr; // As it may change every frame (also used in main render and tied to the number of image in the swapchain), keep a reference to it to
										  // write it right before updates

	Fence				_fence;
	PipelineLayout		_pipelineLayout;
	DescriptorPool		_descriptorPool;
	DescriptorSetLayout _descriptorSetLayout;
	CommandPool			_commandPool;
	CommandBuffers		_commandBuffers;
	Pipeline			_traceRaysPipeline;
	ShaderBindingTable	_shaderBindingTable;
	Pipeline			_updateIrradiancePipeline;
	Pipeline			_updateDepthPipeline;
	Pipeline			_copyBordersPipeline;
	Pipeline			_pipelineProbeInit;
	ShaderBindingTable	_probeInitShaderBindingTable;

	Buffer		 _gridInfoBuffer;
	DeviceMemory _gridInfoMemory;
	Buffer		 _probeInfoBuffer;
	DeviceMemory _probeInfoMemory;

	// Exposed results
	Image	  _irradiance;
	ImageView _irradianceView;
	Image	  _depth;
	ImageView _depthView;

	// Working buffers
	std::vector<ProbeInfo> _probesState;
	DeviceMemory		   _probesToUpdateMemory;
	Buffer				   _probesToUpdate;
	Image				   _rayIrradianceDepth;
	ImageView			   _rayIrradianceDepthView;
	Image				   _rayDirection;
	ImageView			   _rayDirectionView;
	Image				   _workIrradiance;
	ImageView			   _workIrradianceView;
	Image				   _workDepth;
	ImageView			   _workDepthView;
	uint32_t			   _lastUpdateOffset = 0; // The last update stopped on this offset in the "To Update" array.

	QueryPool			 _queryPool;
	RollingBuffer<float> _computeTimes;
	RollingBuffer<float> _traceTimes;
	RollingBuffer<float> _updateTimes;
	RollingBuffer<float> _borderCopyTimes;
	RollingBuffer<float> _copyTimes;

	void	 writeLightDescriptor();
	uint32_t selectProbesToUpdate();
};
