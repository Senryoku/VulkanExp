#pragma once

#include <array>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>

#include "Chunk.hpp"
#include "PerlinNoise.hpp"

class VoxelTerrain {
  public:
	constexpr static size_t Size = 16;

	std::unique_ptr<std::array<Chunk, Size * Size * Size>> chunks{new std::array<Chunk, Size * Size * Size>()};

	inline Chunk&		operator()(size_t i, size_t j, size_t k) { return (*chunks)[i * Size * Size + j * Size + k]; }
	inline const Chunk& operator()(size_t i, size_t j, size_t k) const { return (*chunks)[i * Size * Size + j * Size + k]; }

	// Fill the voxel at position pos in the terrain coordinate system.
	size_t add(glm::vec3 pos) {
		// FIXME: Chunk Meshes are using blocks CENTERED on the voxel point, correct for that:
		pos += glm::vec3(0.5);
		if(glm::any(glm::lessThan(pos, glm::vec3(0))) || glm::any(glm::greaterThan(pos, glm::vec3(Size * Chunk::Size))))
			return -1;
		auto chunkIdx = to1D(pos / static_cast<float>(Chunk::Size));
		auto coordWithinChunk = glm::mod(pos, static_cast<float>(Chunk::Size));
		(*chunks)[chunkIdx](coordWithinChunk.x, coordWithinChunk.y, coordWithinChunk.z).type = 1;
		print("Pos {} ChunkIdx {} coordWithinChunk {}\n", glm::to_string(pos), chunkIdx, glm::to_string(coordWithinChunk));
		return chunkIdx;
	}

	void generate() {
		// FIXME: Test function to have something to display. Remove at some point?
		PerlinNoise			  pn;
		const double		  scale = (1.0 / 64.0);
		ThreadPool::TaskQueue computeChunks;
		for(auto idx = 0; auto& chunk : *chunks) {
			computeChunks.start([idx, scale, &pn, &chunk]() {
				glm::vec3 offset = position(to3D(idx));
				for(int i = 0; i < Chunk::Size; ++i)
					for(int j = 0; j < Chunk::Size; ++j)
						for(int k = 0; k < Chunk::Size; ++k) {
							chunk(i, j, k).type = pn(scale * (offset.x + i), scale * (offset.y + j), scale * (offset.z + k)) > 0.2 ? 1 : Voxel::Empty;
						}
			});
			++idx;
		}
	}

	static inline constexpr glm::vec3 position(size_t i, size_t j, size_t k) { return static_cast<float>(Chunk::Size) * glm::vec3(i, j, k); }
	static inline constexpr glm::vec3 position(glm::ivec3 coords) { return static_cast<float>(Chunk::Size) * glm::vec3(coords.x, coords.y, coords.z); }
	static inline glm::mat4			  transform(size_t idx) {
				  const auto coords = to3D(idx);
				  return transform(coords.x, coords.y, coords.z);
	}
	static inline glm::mat4 transform(size_t i, size_t j, size_t k) { return glm::translate(glm::mat4(1.0), position(i, j, k)); }

	static inline constexpr size_t	   to1D(const glm::ivec3 c) { return to1D(c.x, c.y, c.z); }
	static inline constexpr size_t	   to1D(size_t i, size_t j, size_t k) { return i * Size * Size + j * Size + k; }
	static inline constexpr glm::ivec3 to3D(size_t idx) { return {idx / (Size * Size), (idx / Size) % Size, idx % Size}; }

  private:
};
