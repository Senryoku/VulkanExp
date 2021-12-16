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

#include "common.glsl"
#include "irradiance.glsl"
#include "pbrMetallicRoughness.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0) uniform sampler2D textures[];
layout(binding = 2, set = 0) buffer VerticesBlock { vec4 Vertices[]; };
layout(binding = 3, set = 0) buffer Indices { uint  i[]; } indices;
layout(binding = 4, set = 0) buffer Offsets { uint  o[]; } offsets;
layout(binding = 5, set = 0) buffer MaterialsBlock { uint Materials[]; };
layout(binding = 6, set = 0) uniform UBOBlock {
	ProbeGrid grid;
};
layout(binding = 7, set = 0) uniform sampler2D irradianceColor;
layout(binding = 8, set = 0) uniform sampler2D irradianceDepth;

#include "Vertex.glsl"
#include "Material.glsl"
#include "rayPayload.glsl"

layout(location = 0) rayPayloadInEXT rayPayload payload;
layout(location = 1) rayPayloadEXT bool isShadowed;

hitAttributeEXT vec3 attribs;

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

// FIXME: To Uniforms?
struct Light {
	uint type; // 0 Directional, 1 Point light
	vec3 color;
	vec3 direction;
};

Light Lights[3] = {
	Light(0, vec3(4.0), normalize(vec3(-1, 7, 2))),
	Light(1, vec3(30000.0, 10000.0, 10000.0), vec3(-620, 160, 143.5)),
	Light(1, vec3(30000.0, 10000.0, 10000.0), vec3(487, 160, 143.5))
};

void main()
{		
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	
	vec3 position = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	
	uint materialInstanceIndex = offsets.o[3 * gl_InstanceID + 0];
	uint vertexInstanceOffset = offsets.o[3 * gl_InstanceID + 1];
	uint indexInstanceOffset = offsets.o[3 * gl_InstanceID + 2];
	uint indexOffset = indexInstanceOffset + 3 * gl_PrimitiveID;

	ivec3 index = ivec3(indices.i[indexOffset], indices.i[indexOffset + 1], indices.i[indexOffset + 2]);
	Vertex v0 = unpack(vertexInstanceOffset + index.x);
	Vertex v1 = unpack(vertexInstanceOffset + index.y);
	Vertex v2 = unpack(vertexInstanceOffset + index.z);
	Material m = unpackMaterial(materialInstanceIndex);
	vec2 texCoord = v0.texCoord * barycentricCoords.x + v1.texCoord * barycentricCoords.y + v2.texCoord * barycentricCoords.z;
	vec4 tangent = v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z;
	vec3 normal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
	vec3 bitangent = cross(normal, tangent.xyz) * tangent.w;
	
	vec4 grad = texDerivative(position, normal, v0, v1, v2, payload.raydx, payload.raydy); 
	vec4 texColor = textureGrad(textures[m.albedoTexture], texCoord, grad.xy, grad.zw);

	// If the material has a normal texture, "bend" the normal according to the normal map
	if(m.normalTexture != -1) {
		vec3 tangentSpaceNormal = normalize(2.0 * textureGrad(textures[m.normalTexture], texCoord, grad.xy, grad.zw).rgb - 1.0);
		normal = normalize(mat3(tangent.xyz, bitangent, normal) * tangentSpaceNormal);
	}
	
	vec3 emissiveLight = vec3(0);
	if(m.emissiveTexture != -1) {
		emissiveLight = m.emissiveFactor * textureGrad(textures[m.emissiveTexture], texCoord, grad.xy, grad.zw).rgb;
	}

	vec3 color = vec3(0) + emissiveLight;

	if(payload.depth > 0 && payload.depth < gl_HitTEXT)
		color += payload.color.rgb;

	vec3 indirectLight = sampleProbes(position, normal, grid, irradianceColor, irradianceDepth);  
	color += indirectLight * texColor.rgb;

	for(int i = 0; i < Lights.length(); ++i) {
		isShadowed = true;
		vec3 direction = Lights[i].type == 0 ? Lights[i].direction : normalize(Lights[i].direction - position);
		float tmax = Lights[i].type == 0 ? 10000.0 : length(Lights[i].direction - position);
		traceRayEXT(topLevelAS,            // acceleration structure
					gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,             // rayFlags
					0xFF,                  // cullMask
					0,                     // sbtRecordOffset
					0,                     // sbtRecordStride
					1,                     // missIndex
					position,              // ray origin
					0.1,                   // ray min range
					direction,             // ray direction
					tmax,                  // ray max range
					1                      // payload (location = 1)
		);
		float attenuation = 1.0;
		if(isShadowed) {
			attenuation = 0.005;
		} else {
			attenuation = 1.0;
		}

		// Basic Light Model
		//color += attenuation * clamp(dot(LightDir, normal), 0.2, 1.0) * LightColor * texColor.rgb;

		vec3 lightColor = Lights[i].type == 0 ? Lights[i].color : Lights[i].color / ((length(Lights[i].direction - position) + 1) * (length(Lights[i].direction - position) + 1));
		// Hopefully a better one someday :) - Missing the specular rn, so way darker
		vec3 specularLight = indirectLight; // FIXME: Should trace another ray in primary rays of the full ray traced path; Revert to just the indirect light as a cheap alternative for everything else.
		color += pbrMetallicRoughness(normal, normalize(-gl_WorldRayDirectionEXT), attenuation * lightColor, Lights[i].direction, indirectLight, texColor, m.metallicFactor, m.roughnessFactor).rgb;
	}

	payload.color = vec4(color, texColor.a);
	payload.depth = gl_HitTEXT;
}
