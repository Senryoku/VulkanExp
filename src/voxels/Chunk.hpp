#pragma once

#include <array>
#include <cstdint>

struct Voxel {
	constexpr static uint8_t Empty = -1;

	uint8_t type = Empty;
};

struct Chunk {
	constexpr static size_t Size = 8;

	std::array<Voxel, Size * Size * Size> voxels;

	inline Voxel&		operator()(size_t i, size_t j, size_t k) { return voxels[i * Chunk::Size * Chunk::Size + j * Chunk::Size + k]; }
	inline const Voxel& operator()(size_t i, size_t j, size_t k) const { return voxels[i * Chunk::Size * Chunk::Size + j * Chunk::Size + k]; }

	inline static constexpr size_t to1D(size_t i, size_t j, size_t k) { return i * Chunk::Size * Chunk::Size + j * Chunk::Size + k; }
};

class Mesh;
Mesh generateMesh(const Chunk&);
