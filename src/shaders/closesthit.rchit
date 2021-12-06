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
layout(binding = 7, set = 0) buffer Materials { uint m[]; } materials;

struct rayPayload {
	vec3 raydx;
	vec3 raydy;

	vec3 color; // Result
};

layout(location = 0) rayPayloadInEXT rayPayload payload;
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

struct Material {
	float metallicFactor;
	float roughnessFactor;
	uint albedoTexture;
	uint normalTexture;
	uint metallicRoughnessTexture;
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

Material unpackMaterial(uint index) {
	uint offset = index * 5;
	Material m;
	m.metallicFactor = uintBitsToFloat(materials.m[offset + 0]);
	m.roughnessFactor = uintBitsToFloat(materials.m[offset + 1]);
	m.albedoTexture = materials.m[offset + 2];
	m.normalTexture = materials.m[offset + 3];
	m.metallicRoughnessTexture = materials.m[offset + 4];
	return m;
}

vec3 lightDir = normalize(vec3(-1, 4, 1));

// Tracing Ray Differentials http://graphics.stanford.edu/papers/trd/trd.pdf
// https://github.com/kennyalive/vulkan-raytracing/blob/master/src/shaders/rt_utils.glsl
vec4 texDerivative(vec3 worldPosition, vec3 normal, Vertex v0, Vertex v1, Vertex v2, vec3 raydx, vec3 raydy) {
    vec3 dpdu, dpdv;
    vec3 p01 = v1.pos - v0.pos;
    vec3 p02 = v2.pos - v0.pos;

	vec2 tex01 = v1.texCoord - v0.texCoord;
	vec2 tex02 = v2.texCoord - v0.texCoord;

    float det = tex01[0] * tex02[1] - tex01[1] * tex02[0];
    if (abs(det) < 1e-10) {
		dpdu = normalize(abs(normal.x) > abs(normal.y) ? 
				vec3(-normal.z, 0, normal.x) : 
				vec3(0, -normal.z, normal.y)
		);
		dpdv = cross(normal, dpdu);
    } else {
        float inv_det = 1.0/det;
        dpdu = ( tex02[1] * p01 - tex01[1] * p02) * inv_det;
        dpdv = (-tex02[0] * p01 + tex01[0] * p02) * inv_det;
    }

	// Compute intersection of primitive plane and differential rays (raydx and raydy).
	float tx = dot((worldPosition - gl_WorldRayOriginEXT), normal) / dot(raydx, normal);
	float ty = dot((worldPosition - gl_WorldRayOriginEXT), normal) / dot(raydy, normal);
	vec3 dpdx = (gl_WorldRayOriginEXT + raydx * tx) - worldPosition;
	vec3 dpdy = (gl_WorldRayOriginEXT + raydy * ty) - worldPosition;

	float dudx = 0, dvdx = 0, dudy = 0, dvdy = 0;
    {
        uint dim0 = 0, dim1 = 1;
        vec3 a = abs(normal);
        if (a.x > a.y && a.x > a.z) {
            dim0 = 1;
            dim1 = 2;
        } else if (a.y > a.z) {
            dim0 = 0;
            dim1 = 2;
        }

        float a00 = dpdu[dim0];
		float a01 = dpdv[dim0];
        float a10 = dpdu[dim1];
		float a11 = dpdv[dim1];

        float det = a00 * a11 - a01 * a10;
        if (abs(det) > 1e-10) {
            float inv_det = 1.0/det;
            dudx = ( a11 * dpdx[dim0] - a01 * dpdx[dim1]) * inv_det;
            dvdx = (-a10 * dpdx[dim0] - a00 * dpdx[dim1]) * inv_det;

            dudy = ( a11 * dpdy[dim0] - a01 * dpdy[dim1]) * inv_det;
            dvdy = (-a10 * dpdy[dim0] - a00 * dpdy[dim1]) * inv_det;
        }
    }

	return vec4(dudx, dvdx, dudy, dvdy);
}


void main()
{		
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	
	vec3 P = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	
	uint vertexInstanceOffset = offsets.o[2 * gl_InstanceID];
	uint indexInstanceOffset = offsets.o[2 * gl_InstanceID + 1];
	uint indexOffset = indexInstanceOffset + 3 * gl_PrimitiveID;

	ivec3 index = ivec3(indices.i[indexOffset], indices.i[indexOffset + 1], indices.i[indexOffset + 2]);
	Vertex v0 = unpack(vertexInstanceOffset + index.x);
	Vertex v1 = unpack(vertexInstanceOffset + index.y);
	Vertex v2 = unpack(vertexInstanceOffset + index.z);
	Material m = unpackMaterial(v0.material);
	vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
	vec4 tangent = v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z;
	vec3 normal = v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z;
	vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;
	
	vec4 grad = texDerivative(P, normal, v0, v1, v2, payload.raydx, payload.raydy); 
	vec3 texColor = textureGrad(textures[m.albedoTexture], texCoord, grad.xy, grad.zw).xyz;

	isShadowed = true;
	traceRayEXT(topLevelAS,        // acceleration structure
				gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT,             // rayFlags
				0xFF,              // cullMask
				0,                 // sbtRecordOffset
				0,                 // sbtRecordStride
				1,                 // missIndex
				P,                 // ray origin
				0.1,               // ray min range
				lightDir,          // ray direction
				10000,             // ray max range
				1                  // payload (location = 1)
	);
	float attenuation = 1.0;
	if(isShadowed) {
		attenuation = 0.3;
	} else {
		attenuation = 1.0;
	}

    vec3 tangentSpaceNormal = normalize(2.0 * textureGrad(textures[m.normalTexture], texCoord, grad.xy, grad.zw).rgb - 1.0);
	vec3 finalNormal = mat3(tangent.xyz, bitangent, normal) * tangentSpaceNormal;

    vec3 color = clamp(dot(lightDir, finalNormal), 0.2, 1.0) * texColor.rgb;

	payload.color = attenuation * color;
	
}
