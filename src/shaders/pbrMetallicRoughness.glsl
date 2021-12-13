/*
 * Attempt at interpreting the pbr informations of the default glTF material model
 */

#include "common.glsl"

vec3 specularReflection(vec3 specularEnvironmentR0, vec3 specularEnvironmentR90, float VdotH)
{
	return specularEnvironmentR0 + (specularEnvironmentR90 - specularEnvironmentR0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

// This calculates the specular geometric attenuation (aka G()),
// where rougher material will reflect less light back to the viewer.
float geometricOcclusion(float NdotL, float NdotV, float alphaRoughness)
{
	float attenuationL = 2.0 * NdotL / (NdotL + sqrt(alphaRoughness * alphaRoughness + (1.0 - alphaRoughness * alphaRoughness) * (NdotL * NdotL)));
	float attenuationV = 2.0 * NdotV / (NdotV + sqrt(alphaRoughness * alphaRoughness + (1.0 - alphaRoughness * alphaRoughness) * (NdotV * NdotV)));
	return attenuationL * attenuationV;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float microfacetDistribution(float alphaRoughness, float NdotH)
{
	float roughnessSq = alphaRoughness * alphaRoughness;
	float f = (NdotH * roughnessSq - NdotH) * NdotH + 1.0;
	return roughnessSq / (pi * f * f);
}

 /* https://github.com/KhronosGroup/glTF-WebGL-PBR
  * view: Point to Camera normalize direction
  */
 vec4 pbrMetallicRoughness(vec3 normal, vec3 view, vec3 lightColor, vec3 lightDirection, vec3 specularLight, vec4 albedo, float metallicFactor, float roughnessFactor) {

	vec3 f0 = vec3(0.04);
	vec3 diffuseColor = albedo.rgb * (1.0 - f0);
	diffuseColor *= (1.0 - metallicFactor);

	float alphaRoughness = roughnessFactor * roughnessFactor;
	vec3 specularColor = mix(f0, diffuseColor, metallicFactor);
	float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
	
	// For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
	// For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
	float reflectance90 = clamp(reflectance * 25.0, 0.0, 1.0);
	vec3 specularEnvironmentR0 = specularColor.rgb;
	vec3 specularEnvironmentR90 = vec3(1.0, 1.0, 1.0) * reflectance90;

	vec3 n = normal;
	vec3 v = view;
	vec3 l = normalize(lightDirection);       // Vector from surface point to light
	vec3 h = normalize(l + v);                // Half vector between both l and v
	vec3 reflection = -normalize(reflect(v, n));
	reflection.y *= -1.0f;

	float NdotL = clamp(dot(n, l), 0.001, 1.0);
	float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
	float NdotH = clamp(dot(n, h), 0.0, 1.0);
	float LdotH = clamp(dot(l, h), 0.0, 1.0);
	float VdotH = clamp(dot(v, h), 0.0, 1.0);
	
	// Specular Reflection
	vec3 F = specularReflection(specularEnvironmentR0, specularEnvironmentR90, VdotH);
	float G = geometricOcclusion(NdotL, NdotV, alphaRoughness);
	float D = microfacetDistribution(alphaRoughness, NdotH);
	
	// Calculation of analytical lighting contribution
	vec3 diffuseContrib =  (1.0 - F) * diffuseColor;
	vec3 specContrib =  F * G * D / (4.0 * NdotL * NdotV);

	// Obtain final intensity as reflectance (BRDF) scaled by the energy of the light (cosine law)
	vec3 color = NdotL * lightColor * (diffuseContrib + specContrib);

	// Todo: Specular highlight from raytraced reflections
	//color += specularLight * (specularColor * brdf.x + brdf.y);
	// TEMP:
	color += specularLight * (specularColor * NdotV);

	// Apply optional PBR terms for additional (optional) shading
	/*
	const float u_OcclusionStrength = 1.0f;
	if (material.occlusionTextureSet > -1) {
		float ao = texture(aoMap, (material.occlusionTextureSet == 0 ? inUV0 : inUV1)).r;
		color = mix(color, color * ao, u_OcclusionStrength);
	}
	*/

	return vec4(color, albedo.a);
}