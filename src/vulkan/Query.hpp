#pragma once

#include <Device.hpp>
#include <HandleWrapper.hpp>

class Query : HandleWrapper<uint32_t> {};

class QueryPool : HandleWrapper<VkQueryPool> {
  public:
	~QueryPool() { destroy(); }

	struct QueryResultWithAvailability {
		uint64_t result = 0;
		uint64_t available = 0;
	};

	void create(const Device& device, VkQueryType type, uint32_t queryCount, VkQueryPipelineStatisticFlags statiscticFlags = 0) {
		VkQueryPoolCreateInfo info{
			.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.queryType = type,
			.queryCount = queryCount,
			.pipelineStatistics = statiscticFlags,
		};
		VK_CHECK(vkCreateQueryPool(device, &info, nullptr, &_handle));
		_device = &device;
		_queryCount = queryCount;
	}

	void destroy() {
		if(isValid()) {
			vkDestroyQueryPool(*_device, _handle, nullptr);
			_handle = VK_NULL_HANDLE;
			_device = nullptr;
		}
	}

	inline void begin(VkCommandBuffer commandBuffer, uint32_t queryIndex) { vkCmdBeginQuery(commandBuffer, _handle, queryIndex, 0); }
	inline void end(VkCommandBuffer commandBuffer, uint32_t queryIndex) { vkCmdEndQuery(commandBuffer, _handle, queryIndex); }
	inline void writeTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits stage, uint32_t queryIndex) {
		vkCmdWriteTimestamp(commandBuffer, stage, _handle, queryIndex);
	}

	[[nodiscard]] inline std::vector<QueryResultWithAvailability> get() { return get(0, _queryCount); }

	[[nodiscard]] std::vector<QueryResultWithAvailability> get(uint32_t fistQueryIndex, uint32_t count) {
		std::vector<QueryResultWithAvailability> results;
		results.resize(count);
		vkGetQueryPoolResults(*_device, _handle, fistQueryIndex, count, sizeof(QueryResultWithAvailability) * results.size(), results.data(), 0,
							  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
		return results;
	}

	void reset(VkCommandBuffer commandBuffer, uint32_t fistQueryIndex, uint32_t count = 1) const { vkCmdResetQueryPool(commandBuffer, _handle, fistQueryIndex, count); }
	void reset(VkCommandBuffer commandBuffer) const { reset(commandBuffer, 0, _queryCount); }

	void reset(uint32_t fistQueryIndex, uint32_t count = 1) const { vkResetQueryPool(*_device, _handle, fistQueryIndex, count); }
	void reset() const { reset(static_cast<uint32_t>(0), _queryCount); }

	bool newSampleFlag = false;

  private:
	const Device* _device = nullptr;
	uint32_t	  _queryCount = 0;
};
