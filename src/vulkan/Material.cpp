#include "Material.hpp"

#include <array>

#include <Serialization.hpp>

Material parseMaterial(const JSON::value& mat, uint32_t textureOffset) {
	Material material;
	material.name = mat("name", std::string("NoName"));
	if(mat.contains("pbrMetallicRoughness")) {
		material.properties.baseColorFactor = mat["pbrMetallicRoughness"].get("baseColorFactor", glm::vec4{1.0, 1.0, 1.0, 1.0});
		material.properties.metallicFactor = mat["pbrMetallicRoughness"].get("metallicFactor", 1.0f);
		material.properties.roughnessFactor = mat["pbrMetallicRoughness"].get("roughnessFactor", 1.0f);
		if(mat["pbrMetallicRoughness"].contains("baseColorTexture")) {
			material.properties.albedoTexture = textureOffset + mat["pbrMetallicRoughness"]["baseColorTexture"]["index"].as<int>();
		}
		if(mat["pbrMetallicRoughness"].contains("metallicRoughnessTexture")) {
			material.properties.metallicRoughnessTexture = textureOffset + mat["pbrMetallicRoughness"]["metallicRoughnessTexture"]["index"].as<int>();
		}
	}
	material.properties.emissiveFactor = mat.get("emissiveFactor", glm::vec3(0.0f));
	if(mat.contains("emissiveTexture"))
		material.properties.emissiveTexture = textureOffset + mat["emissiveTexture"]["index"].as<int>();

	if(mat.contains("normalTexture"))
		material.properties.normalTexture = textureOffset + mat["normalTexture"]["index"].as<int>();
	return material;
}

JSON::value toJSON(const Material& mat) {
	JSON::object obj;

	obj["name"] = mat.name;
	obj["pbrMetallicRoughness"] = JSON::object();
	obj["pbrMetallicRoughness"]["baseColorFactor"] = toJSON(mat.properties.baseColorFactor);
	obj["pbrMetallicRoughness"]["metallicFactor"] = mat.properties.metallicFactor;
	obj["pbrMetallicRoughness"]["roughnessFactor"] = mat.properties.roughnessFactor;
	auto baseColorTexture = JSON::object();
	baseColorTexture["index"] = mat.properties.albedoTexture;
	obj["pbrMetallicRoughness"]["baseColorTexture"] = baseColorTexture;
	auto metallicRoughnessTexture = JSON::object();
	metallicRoughnessTexture["index"] = mat.properties.metallicRoughnessTexture;
	obj["pbrMetallicRoughness"]["metallicRoughnessTexture"] = metallicRoughnessTexture;
	obj["normalTexture"] = mat.properties.normalTexture;
	obj["emissiveFactor"] = toJSON(mat.properties.emissiveFactor);
	obj["emissiveTexture"] = mat.properties.emissiveTexture;

	return obj;
}
