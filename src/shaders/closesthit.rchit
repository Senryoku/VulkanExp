#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable

#include "common.glsl"
#include "pbrMetallicRoughness.glsl"
#include "Lights.glsl"
#include "ProbeGrid.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0) uniform sampler2D textures[];
layout(binding = 2, set = 0) buffer VerticesBlock { vec4 Vertices[]; };
layout(binding = 3, set = 0) buffer Indices { uint  i[]; } indices;
layout(binding = 4, set = 0) buffer Offsets { uint  o[]; } offsets;
layout(binding = 5, set = 0) buffer MaterialsBlock { uint Materials[]; };
layout(binding = 6, set = 0) uniform UBOBlock {
	ProbeGrid grid;
};
layout(binding = 7, set = 0) buffer ProbesBlock { uint Probes[]; };
layout(binding = 8, set = 0) uniform sampler2D irradianceColor;
layout(binding = 9, set = 0) uniform sampler2D irradianceDepth;
layout(binding = 10, set = 0) uniform UBOLight {
	Light DirectionalLight;
};

#include "irradiance.glsl"
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
	
	uint materialInstanceIndex = offsets.o[3 * gl_InstanceCustomIndexEXT + 0];
	uint vertexInstanceOffset = offsets.o[3 * gl_InstanceCustomIndexEXT + 1];
	uint indexInstanceOffset = offsets.o[3 * gl_InstanceCustomIndexEXT + 2];
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
	vec4 texColor = m.albedoTexture != -1 ? textureGrad(textures[m.albedoTexture], texCoord, grad.xy, grad.zw) : vec4(1.0);
	
	// If the material has a normal texture, "bend" the normal according to the normal map
	if(m.normalTexture != -1) {
		vec4 tangentData = v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z;
		vec3 tangent = normalize(vec3(tangentData.xyz * gl_WorldToObjectEXT)); // To world space
		float tangentHandedness = tangentData.w;
		vec3 bitangent = cross(normal, tangent) * tangentHandedness;
	
		vec3 mappedNormal = normalize(2.0 * textureGrad(textures[m.normalTexture], texCoord, grad.xy, grad.zw).rgb - 1.0);
		normal = normalize(mat3(tangent, bitangent, normal) * mappedNormal);
	}
	
	float metalness = m.metallicFactor;
	float roughness = m.roughnessFactor;
	if(m.metallicRoughnessTexture != -1) {
		vec4 metallicRoughnessTexture = textureGrad(textures[m.metallicRoughnessTexture], texCoord, grad.xy, grad.zw);
		metalness *= metallicRoughnessTexture.b;
		roughness *= metallicRoughnessTexture.g;
	}
	
	vec3 emissiveLight = m.emissiveFactor;
	if(m.emissiveTexture != -1) {
		emissiveLight *= textureGrad(textures[m.emissiveTexture], texCoord, grad.xy, grad.zw).rgb;
	}

	vec3 color = vec3(0) + emissiveLight;

	vec3 indirectLight = sampleProbes(position, normal, -gl_WorldRayDirectionEXT, grid, irradianceColor, irradianceDepth);  
	color += indirectLight * texColor.rgb;	
	
	float tmax = 10000.0;
	/*
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
				0                      // payload location 0
		);
		--payload.recursionDepth;
		specularLight = payload.color.rgb;
	}
	*/

	// Direct lighting
	isShadowed = true;
	vec3 shadowBiasedPosition = position;
	#if 0
	// Shadow offset described in Ray Tracing Gems II (https://link.springer.com/content/pdf/10.1007%2F978-1-4842-7185-8.pdf)
	// Sometimes helps (avoid small harsh shadows), but isn't very convincing overall.
	vec3 tmpu = position - v0.pos;
	vec3 tmpv = position - v1.pos;
	vec3 tmpw = position - v2.pos;
	float dotu = min(0.0, dot(tmpu, v0.normal));
	float dotv = min(0.0, dot(tmpu, v1.normal));
	float dotw = min(0.0, dot(tmpu, v2.normal));
	tmpu -= dotu * v0.normal;
	tmpv -= dotv * v1.normal;
	tmpw -= dotw * v2.normal;
	shadowBiasedPosition += barycentricCoords.x * tmpu + barycentricCoords.y * tmpv + barycentricCoords.z * tmpw;
	#endif
	traceRayEXT(topLevelAS,            // acceleration structure
		gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,             // rayFlags
		0xFF,                  // cullMask
		0,                     // sbtRecordOffset
		0,                     // sbtRecordStride
		1,                     // missIndex
		shadowBiasedPosition,              // ray origin
		0.1,                   // ray min range
		DirectionalLight.direction.xyz,             // ray direction
		tmax,                  // ray max range
		1                      // payload (location = 1)
	);
	if(!isShadowed) {
		// FIXME: IDK, read stuff https://learnopengl.com/PBR/Lighting
		color += pbrMetallicRoughness(normal, normalize(-gl_WorldRayDirectionEXT), DirectionalLight.color.rgb, DirectionalLight.direction.xyz, texColor, metalness, roughness).rgb;
	}

	payload.color = vec4(color, texColor.a);
}
