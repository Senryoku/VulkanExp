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

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool isShadowed;

hitAttributeEXT vec3 attribs;

void main()
{		
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	
	vec3 P = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

	hitValue = barycentricCoords;

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
		
	hitValue = attenuation * barycentricCoords;
	
}
