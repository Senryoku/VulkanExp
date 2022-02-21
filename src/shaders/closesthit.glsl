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

#ifndef NO_REFLECTION
layout(binding = 11, set = 0) uniform UniformBufferObject 
{
    mat4 view;
    mat4 proj;
	uint frameIndex;
} ubo;
layout(set = 1, binding = 0) uniform sampler blueNoiseSampler;
layout(set = 1, binding = 1) uniform texture2D blueNoiseTextures[64];
#endif

#include "irradiance.glsl"
#include "Vertex.glsl"
#include "unpackMaterial.glsl"
#include "rayPayload.glsl"

layout(location = 0) rayPayloadInEXT rayPayload payload;
layout(location = 1) rayPayloadEXT bool isShadowed;

hitAttributeEXT vec2 attribs;

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


vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness*roughness;
	
    float phi = 2.0 * pi * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
	
    // from spherical coordinates to cartesian coordinates
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
	
    // from tangent-space vector to world-space sample vector
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
	
    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
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
	
	vec3 position = gl_WorldRayDirectionEXT * gl_HitTEXT + gl_WorldRayOriginEXT;
	
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
	vec4 albedo = vec4(m.baseColorFactor, 1.0);
	if(m.albedoTexture != -1) {
		 vec4 texColor = textureGrad(textures[m.albedoTexture], texCoord, grad.xy, grad.zw);
		 albedo *= texColor;
	 }
	
	// If the material has a normal texture, "bend" the normal according to the normal map
	vec4 tangentData = v0.tangent * barycentricCoords.x + v1.tangent * barycentricCoords.y + v2.tangent * barycentricCoords.z;
	vec3 tangent = normalize(vec3(tangentData.xyz * gl_WorldToObjectEXT)); // To world space
	if(m.normalTexture != -1) {
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

	float tmax = 10000.0;

	vec3 color = vec3(0) + emissiveLight;
		
	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = albedo.rgb * (1.0 - f0);
	diffuseColor *= (1.0 - metalness);

	// Specular (Use irradiance from prbes as a crude approximation on secondary rays)
	vec3 specularColor = mix(f0, albedo.rgb, metalness);
	vec3 reflectDir = reflect(gl_WorldRayDirectionEXT, normal);
	vec3 reflection;
#ifndef NO_REFLECTION
	if(payload.recursionDepth == 0) {
		++payload.recursionDepth;
		const uint SAMPLE_COUNT = 32u;
		float totalWeight = 0.0;   
		vec3 prefilteredColor = vec3(0.0);     
		for(uint i = 0u; i < SAMPLE_COUNT; ++i)
		{
		#if 0
			vec2 Xi = Hammersley(i, SAMPLE_COUNT);
			vec3 H  = ImportanceSampleGGX(Xi, normal, roughness);
			vec3 L  = normalize(2.0 * dot(-gl_WorldRayDirectionEXT, H) * H + gl_WorldRayDirectionEXT);
		#else
			// It makes no sense to change this from frame to frame as long as we don't have temporal accumulation (which we won't add as long as this stays only used a debug view only)
			//vec2 noise = texture(sampler2D(blueNoiseTextures[(SAMPLE_COUNT * ubo.frameIndex + i) % 64], blueNoiseSampler), vec2((gl_LaunchIDEXT.xy + ubo.frameIndex / (SAMPLE_COUNT * 64.0)) / 64.0)).xy;
			vec2 noise = texture(sampler2D(blueNoiseTextures[i % 64], blueNoiseSampler), vec2((gl_LaunchIDEXT.xy) / 64.0)).xy;
			float theta = roughness * (noise.x - 0.5) * pi;
			float phi = (noise.y - 0.5) * 2.0f * pi;
			vec3 L = rotateAxis(reflectDir, tangent, theta);
			L = rotateAxis(L, reflectDir, phi);
		#endif
			float NdotL = max(dot(normal, L), 0.0);
			if(NdotL > 0.0)
			{
				payload.raydx = rotateAxis(L, normal, 0.001);
				payload.raydy = rotateAxis(L, cross(normal, L), 0.001);
				traceRayEXT(topLevelAS, 0, 0xff, 0, 0, 0, position, 0.01, L, tmax, 0);
				prefilteredColor += NdotL * payload.color.rgb;
				totalWeight      += NdotL;
			}
		}	
		if(totalWeight > 0)
			prefilteredColor = prefilteredColor / totalWeight;
		reflection = prefilteredColor;
		--payload.recursionDepth;
	} else {
#endif
		reflection = sampleProbes(position, reflectDir, -gl_WorldRayDirectionEXT, grid, irradianceColor, irradianceDepth);
#ifndef NO_REFLECTION
	}
#endif
	color.rgb += specularColor * reflection.rgb;
	
	// Indirect Light (Radiance from probes)
	vec3 indirectLight = sampleProbes(position, normal, -gl_WorldRayDirectionEXT, grid, irradianceColor, irradianceDepth);
	color.rgb += indirectLight * diffuseColor;

	// Direct lighting
	isShadowed = true;
	vec3 shadowBiasedPosition = position;
	#if 0
	// Shadow offset described in Ray Tracing Gems II (https://link.springer.com/content/pdf/10.1007%2F978-1-4842-7185-8.pdf) Chapter 4
	// Sometimes helps (avoid small harsh shadows), but isn't very convincing overall.
	// FIXME: Something is still very wrong.
	vec3 localPos = barycentricCoords.x * v0.pos + barycentricCoords.y * v1.pos + barycentricCoords.z * v2.pos;
	vec3 tmpu = localPos - v0.pos;
	vec3 tmpv = localPos - v1.pos;
	vec3 tmpw = localPos - v2.pos;
	float dotu = min(0.0, dot(tmpu, v0.normal));
	float dotv = min(0.0, dot(tmpv, v1.normal));
	float dotw = min(0.0, dot(tmpw, v2.normal));
	tmpu -= dotu * v0.normal;
	tmpv -= dotv * v1.normal;
	tmpw -= dotw * v2.normal;
	shadowBiasedPosition += vec3(gl_ObjectToWorld3x4EXT * (barycentricCoords.x * tmpu + barycentricCoords.y * tmpv + barycentricCoords.z * tmpw));
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
		color += pbrMetallicRoughness(normal, normalize(-gl_WorldRayDirectionEXT), DirectionalLight.color.rgb, DirectionalLight.direction.xyz, albedo, metalness, roughness).rgb;
	}

	payload.color = vec4(color, albedo.a);
}
