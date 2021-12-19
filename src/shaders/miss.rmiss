#version 460
#extension GL_EXT_ray_tracing : enable

#include "common.glsl"
#include "Lights.glsl"
layout(binding = 9, set = 0) uniform UBOLight {
	Light DirectionalLight;
};

#include "rayPayload.glsl"

layout(location = 0) rayPayloadInEXT rayPayload payload;

/*
 * Atmosphering Scattering
 * See https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering
 */

const vec3 WaveLengths = vec3(0.650f, 0.570f, 0.475f);
const vec3 InvWaveLengths = vec3(1.0f / pow(0.650f, 4.0f), 1.0f / pow(0.570f, 4.0f), 1.0f / pow(0.475f, 4.0f));
const float AvegerageDensityAltitude = 0.25f; // [0, 1] factor of the atmosphere depth
const float OuterRadius = 250.0f;
const float InnerRadius = 200.0f;
const float Scale = 1.0 / (OuterRadius - InnerRadius);

float traceSphereOutside(vec3 center, float radius, vec3 origin, vec3 direction)
{	
    vec3 d = origin - center;
	
	float a = dot(direction, direction);
	float b = dot(direction, d);
	float c = dot(d, d) - radius * radius;
	
	float g = b * b - a * c;
	
	if(g > 0.0)
    {
		float dis = (-sqrt(g) - b) / a;
        return dis;
	}
    return -1;
}


float traceSphereInside(vec3 center, float radius, vec3 origin, vec3 direction) {
	vec3 oc = center - origin;
	float docdir = dot(oc, direction);
	vec3 pc = origin + docdir * direction;
	float dist = sqrt(radius * radius - length(pc - center) * length(pc - center));
	if(docdir > 0)
		return dist - length(pc - origin);
	else 
		return dist + length(pc - origin);
}

float scale(float fCos)
{
	float x = 1.0 - fCos;
	return AvegerageDensityAltitude * exp(-0.00287 + x*(0.459 + x*(3.83 + x*(-6.80 + x*5.25))));
}

const uint SampleCount = 50;

const float Kr = 0.0025f;		// Rayleigh scattering constant
const float Kr4PI = Kr * 4.0f * pi;
const float Km = 0.0010f;		// Mie scattering constant
const float Km4PI = Km * 4.0f * pi;
const float ESun = 20.0f;		// Sun brightness constant
const float KrESun = Kr * ESun;
const float KmESun = Km * ESun;
const float g = -0.990f;		// The Mie phase asymmetry factor

void main()
{
	const vec3 planetCenter = vec3(0, InnerRadius, 0);
	float height = length(gl_WorldRayOriginEXT);
	vec3 lightDir = -normalize(DirectionalLight.direction.xyz);

	// We'll assume gl_WorldRayOriginEXT is inside the atmosphere
	if(height < OuterRadius) {
		float rayDepth = traceSphereInside(vec3(0), OuterRadius, gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT);
		
		float depth = exp(Scale/AvegerageDensityAltitude * (InnerRadius - height));
		float startAngle = dot(gl_WorldRayDirectionEXT, gl_WorldRayOriginEXT) / height;
		float startOffset = depth * scale(startAngle);

		float sampleLength = rayDepth / SampleCount;
		float scaledLength = sampleLength * Scale;
		vec3 sampleRay = gl_WorldRayDirectionEXT * sampleLength;
		vec3 samplePoint = gl_WorldRayOriginEXT + 0.5 * sampleRay;

		vec3 color = vec3(0);
		for(uint i = 0; i < SampleCount; ++i) {
			float height = length(samplePoint);
			float depth = exp(Scale/AvegerageDensityAltitude * (InnerRadius - height));
			float lightAngle = dot(lightDir, samplePoint) / height;
			float cameraAngle = dot(gl_WorldRayDirectionEXT, samplePoint) / height;
			float scatter = startOffset + depth * (scale(lightAngle) - scale(cameraAngle));
			vec3 attenuate = exp(-scatter * (InvWaveLengths * Kr4PI + Km4PI));
			color += attenuate * (depth * scaledLength);
			samplePoint += sampleRay;
		}

		vec3 secondary = color * KmESun;
		color *= InvWaveLengths * KrESun;
		float miecos = dot(lightDir, -gl_WorldRayDirectionEXT);
		float miePhase = 1.5f * ((1.0f - g * g) / (2.0f + g * g)) * (1.0f + miecos * miecos) / pow(1.0f + g * g - 2.0 * g * miecos, 1.5);
		color += miePhase * secondary;
		payload.color.rgb = color;
		//payload.color.rgb = vec3(rayDepth / 4000.0);
	} else { // We're outside the atmosphere
		float depth = traceSphereOutside(vec3(0), OuterRadius, gl_WorldRayOriginEXT, gl_WorldRayDirectionEXT);
		if(depth > 0) {
			payload.color.rgb = 0.5f * vec3(0.5294117647, 0.80784313725, 0.92156862745);
		} else {
			payload.color = vec4(0.0);
		}
	}
	payload.depth = -1.0f;
}