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
#extension GL_EXT_debug_printf : enable

#include "common.glsl"
#include "irradiance.glsl"
#include "pbrMetallicRoughness.glsl"
#include "Lights.glsl"

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
layout(binding = 9, set = 0) uniform UBOLight {
	Light DirectionalLight;
};

#include "Vertex.glsl"
#include "Material.glsl"
#include "rayPayload.glsl"

layout(location = 0) rayPayloadInEXT rayPayload payload;
layout(location = 1) rayPayloadEXT bool isShadowed;

hitAttributeEXT vec3 attribs;

// Tracing Ray Differentials http://graphics.stanford.edu/papers/trd/trd.pdf
// https://github.com/kennyalive/vulkan-raytracing/blob/master/src/shaders/rt_utils.glsl
vec4 texDerivative(vec3 worldPosition, Vertex v0, Vertex v1, Vertex v2, vec3 raydx, vec3 raydy) {
	vec3 dpdu, dpdv;
    vec3 p01 = mat3(gl_ObjectToWorldEXT) * (v1.pos - v0.pos);
    vec3 p02 = mat3(gl_ObjectToWorldEXT) * (v2.pos - v0.pos);
	vec3 normal = normalize(cross(p01, p02));

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
	payload.depth = gl_HitTEXT;
	// Stop early if we're hiting the back face of a triangle. We're most likely inside a wall, this will prevent some light leaks with irradiance probes.
	if(gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT) {
		payload.depth *= 0.80;
		payload.color = vec4(0);
		return;
	}

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
	vec3 tangentSpaceNormal = normalize(v0.normal * barycentricCoords.x + v1.normal * barycentricCoords.y + v2.normal * barycentricCoords.z);
	vec3 normal = normalize(vec3(tangentSpaceNormal * gl_WorldToObjectEXT)); // To world space
		
	vec4 grad = texDerivative(position, v0, v1, v2, payload.raydx, payload.raydy); 
	vec4 texColor = textureGrad(textures[m.albedoTexture], texCoord, grad.xy, grad.zw);
	
	// If the material has a normal texture, "bend" the normal according to the normal map
	if(m.normalTexture != -1) {
		vec4 tangentData = v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z;
		vec3 tangent = normalize(vec3(tangentData.xyz * gl_WorldToObjectEXT)); // To world space
		float tangentHandedness = tangentData.w;
		vec3 bitangent = cross(normal, tangent) * tangentHandedness;
	
		vec3 mappedNormal = normalize(2.0 * textureGrad(textures[m.normalTexture], texCoord, grad.xy, grad.zw).rgb - 1.0);
		normal = normalize(mat3(tangent, bitangent, normal) * mappedNormal);
	}
	
	vec3 emissiveLight = vec3(0);
	if(m.emissiveTexture != -1) {
		emissiveLight = m.emissiveFactor * textureGrad(textures[m.emissiveTexture], texCoord, grad.xy, grad.zw).rgb;
	}

	vec3 color = vec3(0) + emissiveLight;

	vec3 indirectLight = sampleProbes(position, normal, -gl_WorldRayDirectionEXT, grid, irradianceColor, irradianceDepth);  
	color += indirectLight * texColor.rgb;	
	
	float tmax = 10000.0;
	vec3 specularLight = indirectLight; // FIXME: Should trace another ray in primary rays of the full ray traced path; Revert to just the indirect light as a cheap alternative for everything else.
	vec3 reflectDir = reflect(normalize(position - gl_WorldRayOriginEXT), normal);
	if(payload.recursionDepth == 0) {
		++payload.recursionDepth;
		vec2 polar = cartesianToPolar(reflectDir);
		payload.color = vec4(0);
		payload.raydx = polarToCartesian(polar + vec2(0.1f, 0));
		payload.raydy = polarToCartesian(polar + vec2(0, 0.1f));
		traceRayEXT(topLevelAS,        // acceleration structure
				0,                     // rayFlags
				0xFF,                  // cullMask
				0,                     // sbtRecordOffset
				0,                     // sbtRecordStride
				0,                     // missIndex
				position,              // ray origin
				0.1,                   // ray min range
				reflectDir,            // ray direction
				tmax,                  // ray max range
				0                      
		);
		--payload.recursionDepth;
		specularLight = payload.color.rgb;
	}
	vec3 view = normalize(-gl_WorldRayDirectionEXT);
	vec3 halfVector = normal; // normalize(light + (-view)), but here light = reflect(-view, normal)
	float VdotH = clamp(dot(view, halfVector), 0.0, 1.0);
	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = texColor.rgb * (1.0 - f0);
	diffuseColor *= (1.0 - m.metallicFactor);
	vec3 specularColor = mix(f0, diffuseColor, m.metallicFactor);
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
	vec3 specularEnvironmentR0 = specularColor.rgb;	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;
	vec3 F = specularReflection(specularEnvironmentR0, specularEnvironmentR90, VdotH);
	color += (F /*Missing BRDF parameters */) * pbrMetallicRoughness(normal, normalize(-gl_WorldRayDirectionEXT), specularLight, -reflectDir, texColor, m.metallicFactor, m.roughnessFactor).rgb;

	// Direct lighting
	isShadowed = true;
	traceRayEXT(topLevelAS,            // acceleration structure
		gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,             // rayFlags
		0xFF,                  // cullMask
		0,                     // sbtRecordOffset
		0,                     // sbtRecordStride
		1,                     // missIndex
		position,              // ray origin
		0.1,                   // ray min range
		DirectionalLight.direction.xyz,             // ray direction
		tmax,                  // ray max range
		1                      // payload (location = 1)
	);
	if(!isShadowed) {
		// FIXME: IDK, read stuff https://learnopengl.com/PBR/Lighting
		color += pbrMetallicRoughness(normal, normalize(-gl_WorldRayDirectionEXT), DirectionalLight.color.rgb, DirectionalLight.direction.xyz, texColor, m.metallicFactor, m.roughnessFactor).rgb;
	}

	payload.color = vec4(color, texColor.a);
}
