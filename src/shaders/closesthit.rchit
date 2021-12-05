/* Copyright (c) 2019-2020, Sascha Willems
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 3, set = 0) uniform sampler2D textures[];
layout(binding = 4, set = 0) buffer Vertices { vec4 v[]; } vertices;
layout(binding = 5, set = 0) buffer Indices { uint  i[]; } indices;
layout(binding = 6, set = 0) buffer Offsets { uint  o[]; } offsets;

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool isShadowed;

hitAttributeEXT vec3 attribs;


struct Vertex {
	vec3 pos;
	vec3 color;
	vec3 normal;
	vec4 tangent;
	vec2 texCoord;
	uint material;
};

Vertex unpack(uint index)
{
	// Unpack the vertices from the SSBO
	const int vertexSizeInBytes = 4 * 16;
	const int stride = vertexSizeInBytes / 16;

	vec4 d0 = vertices.v[stride * index + 0];
	vec4 d1 = vertices.v[stride * index + 1];
	vec4 d2 = vertices.v[stride * index + 2];
	vec4 d3 = vertices.v[stride * index + 3];

	Vertex v;
	v.pos = d0.xyz;
	v.color = vec3(d0.w, d1.x, d1.y);
	v.normal = vec3(d1.z, d1.w, d2.x);
	v.tangent = vec4(d2.y, d2.z, d2.w, d3.x);
	v.texCoord = vec2(d3.y, d3.z);
	v.material = floatBitsToUint(d3.w);

	return v;
}

void main()
{		
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	
	vec3 P = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	
	uint vertexOffset = offsets.o[2 * gl_InstanceID];
	uint indexOffset = offsets.o[2 * gl_InstanceID + 1];

	ivec3 index = ivec3(indices.i[indexOffset + 3 * gl_PrimitiveID], indices.i[indexOffset + 3 * gl_PrimitiveID + 1], indices.i[indexOffset + 3 * gl_PrimitiveID + 2]);
	Vertex v0 = unpack(vertexOffset + index.x);
	Vertex v1 = unpack(vertexOffset + index.y);
	Vertex v2 = unpack(vertexOffset + index.z);
	vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;

	// TODO: - Get actual Vertex Data (particularly texCoord)
	//       - Get actual Material Data
	vec3 color = texture(textures[min(2 * v0.material, 64)], texCoord).xyz;
	hitValue = color;

	isShadowed = true;
	traceRayEXT(topLevelAS,        // acceleration structure
				gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,             // rayFlags
				0xFF,              // cullMask
				0,                 // sbtRecordOffset
				0,                 // sbtRecordStride
				1,                 // missIndex
				P,                 // ray origin
				0.1,               // ray min range
				normalize(vec3(2, 4, 1)),            // ray direction
				10000,              // ray max range
				1                  // payload (location = 1)
	);
	float attenuation = 1.0;
	if(isShadowed) {
		attenuation = 0.3;
	} else {
		attenuation = 1.0;
	}
		
	hitValue = attenuation * color;
	
}
