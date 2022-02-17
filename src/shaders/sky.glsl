/*
 * Atmosphering Scattering
 * See https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering
 */

 // TODO: Turn these into uniforms
const vec3 WaveLengths = vec3(0.650f, 0.570f, 0.475f);
const vec3 InvWaveLengths = vec3(1.0f / pow(0.650f, 4.0f), 1.0f / pow(0.570f, 4.0f), 1.0f / pow(0.475f, 4.0f));
const float AvegerageDensityAltitude = 0.25f; // [0, 1] factor of the atmosphere depth
const float InnerRadius = 100000.0f;
const float OuterRadius =   2500.0f + InnerRadius;
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
        if(dis > 0) return dis;
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

const uint SampleCount = 64;

const float Kr = 0.0025f;		// Rayleigh scattering constant
const float Kr4PI = Kr * 4.0f * pi;
const float Km = 0.0010f;		// Mie scattering constant
const float Km4PI = Km * 4.0f * pi;
const float g = -0.990f;		// The Mie phase asymmetry factor

// TODO: We should probably precompute this into a cubemap at some point (with a really slow update rate)
// Original value for sunColor: vec3(20);
const float sunBrightnessFactor = 1.0f; // FIXME: Hack
vec3 sky(vec3 rayOrigin, vec3 rayDirection, vec3 sunPosition, vec3 sunColor, bool showSun) {
	//sunColor = normalize(sunColor);
	sunColor *= sunBrightnessFactor;
	
	// Translate so y = -100 is on the planet surface
	const vec3 planetCenter = vec3(0, -InnerRadius - 100, 0);
	vec3 position = rayOrigin - planetCenter;
	float height = length(position);
	vec3 lightDir = normalize(sunPosition);

	// FIXME: Workaround for when we're 'exactly' on the surface
	if(abs(height - InnerRadius) < 1e-3) {
		position += 1e-2 * normalize(position);
		height = length(position);
	}

	if(height < OuterRadius) {
		// Stop at the horizon (Intersection with the planet). 
		if(height > InnerRadius) {
			float planetDistance = traceSphereOutside(vec3(0), InnerRadius, position, rayDirection);
			if(planetDistance >= 0) return max(0.1, dot(lightDir, normalize(position + planetDistance * rayDirection))) * vec3(0.05);
		} else { // We're inside the planet, just display something to help orient ourself (or nothing) :D
			//return dot(vec3(0, 1, 0), rayDirection) * vec3(0.25, 0, 0) + dot(vec3(0, -1, 0), rayDirection) * vec3(0, 0.25, 0);
			return vec3(0);
		}

		float rayDepth = traceSphereInside(vec3(0), OuterRadius, position, rayDirection);
		if(isinf(rayDepth) || isnan(rayDepth)) return vec3(0); // FIXME
		
		float depth = exp(Scale/AvegerageDensityAltitude * (InnerRadius - height));
		float startAngle = dot(rayDirection, position) / height;
		float startOffset = depth * scale(startAngle);

		float sampleLength = rayDepth / SampleCount;
		float scaledLength = sampleLength * Scale;
		vec3 sampleRay = rayDirection * sampleLength;
		vec3 samplePoint = position + 0.5 * sampleRay;

		vec3 color = vec3(0);
		for(uint i = 0; i < SampleCount; ++i) {
			float height = length(samplePoint);
			float depth = exp(Scale/AvegerageDensityAltitude * (InnerRadius - height));
			float lightAngle = dot(lightDir, samplePoint) / height;
			float cameraAngle = dot(rayDirection, samplePoint) / height;
			float scatter = startOffset + depth * (scale(lightAngle) - scale(cameraAngle));
			vec3 attenuate = exp(-scatter * (InvWaveLengths * Kr4PI + Km4PI));
			if(any(isinf(attenuate)) || any(isnan(attenuate))) continue; // FIXME
			color += attenuate * (depth * scaledLength);
			samplePoint += sampleRay;
		}

		vec3 secondary = color * Km * sunColor;	
		color *= InvWaveLengths * Kr * sunColor;
		if(showSun) {
			float miecos = dot(lightDir, -rayDirection);
			float miePhase = 1.5f * ((1.0f - g * g) / (2.0f + g * g)) * (1.0f + miecos * miecos) / pow(max(1e-3, 1.0f + g * g - 2.0 * g * miecos), 1.5);
			color += miePhase * secondary;
		}
		if(!any(isinf(color)))
			return color;
	} else { // We're outside the atmosphere, TODO, or TOIGNORE :)
		float depth = traceSphereOutside(vec3(0), OuterRadius, position, rayDirection);
		if(depth > 0) {
			return dot(lightDir, normalize(position + depth * rayDirection)) * 0.5f * vec3(0.5294117647, 0.80784313725, 0.92156862745);
		}
	}
	return vec3(0);
}