#include "Chunk.hpp"

#include <Mesh.hpp>

Mesh generateMesh(const Chunk& chunk) {
	Mesh m;

	/*
	  3 ---- 7
	 /|     /|
	2 ---- 6 |
	| 1 ---- 5   y z
	|/     |/    |/
	0 ---- 4     0-x
	*/
	static const std::array<std::array<int, 3>, 6> normals = {{{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}}};
	static const std::array<glm::vec3, 6>		   tangents = {{{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}}}; // Check
	static const std::array<glm::vec2, 12>		   texcoord = {};																	   // TODO
	static const std::array<std::array<int, 4>, 6> face_vertices = {{
		{0, 1, 2, 3},
		{5, 1, 4, 0},
		{4, 0, 6, 2},
		{1, 5, 3, 7},
		{5, 4, 7, 6},
		{6, 2, 7, 3},
	}};
	static const glm::vec3						   min{-0.5, -0.5, -0.5};
	static const glm::vec3						   max{0.5, 0.5, 0.5};
	static const std::array<glm::vec3, 8>		   cube_offsets = {min,
														   glm::vec3{min.x, min.y, max.z},
														   glm::vec3{min.x, max.y, min.z},
														   glm::vec3{min.x, max.y, max.z},
														   glm::vec3{max.x, min.y, min.z},
														   glm::vec3{max.x, min.y, max.z},
														   glm::vec3{max.x, max.y, min.z},
														   max};

	static auto is_edge = [](auto i, auto j, auto k) { return i == 0 || j == 0 || k == 0 || i == Chunk::Size - 1 || j == Chunk::Size - 1 || k == Chunk::Size - 1; };

	auto chunk_offset = [&chunk](auto i, auto j, auto k, auto offset) { return chunk(i + offset[0], j + offset[1], k + offset[2]); };
	auto out_of_bounds = [](int i, int j, int k, auto n) {
		return i + n[0] < 0 || j + n[1] < 0 || k + n[2] < 0 || i + n[0] >= static_cast<int>(Chunk::Size) || j + n[1] >= static_cast<int>(Chunk::Size) ||
			   k + n[2] >= static_cast<int>(Chunk::Size);
	};

	m.SubMeshes.emplace_back();

	// TODO: Optimize faces (merge triangles)
	for(size_t i = 0; i < Chunk::Size; ++i)
		for(size_t j = 0; j < Chunk::Size; ++j)
			for(size_t k = 0; k < Chunk::Size; ++k) {
				// Add non-empty voxels
				if(chunk(i, j, k).type != Voxel::Empty) {
					// TODO: Choose SubMesh depending on voxels type (i.e. material)
					auto& sm = m.SubMeshes[0];
					// Always consider voxel on the edge
					bool is_visible = is_edge(i, j, k);
					for(const auto& n : normals)
						is_visible = is_visible || chunk_offset(i, j, k, n).type == Voxel::Empty;
					if(is_visible)
						// Add each relevant (i.e. visible) face
						for(int f = 0; f < 6; ++f) {
							if(out_of_bounds(i, j, k, normals[f]) || chunk_offset(i, j, k, normals[f]).type == Voxel::Empty) {
								for(int v = 0; v < 4; ++v)
									sm.getVertices().emplace_back(Vertex{.pos = glm::vec3(i, j, k) + cube_offsets[face_vertices[f][v]],
																		 .normal = glm::vec3(normals[f][0], normals[f][1], normals[f][2]),
																		 .tangent = glm::vec4(tangents[f], 1.0),
																		 .texCoord = texcoord[face_vertices[f][v]]});
								for(const auto& v : {0, 1, 3, 0, 3, 2})
									sm.getIndices().emplace_back(sm.getVertices().size() - 4 + v);
							}
						}
				}
			}

	return m;
}
