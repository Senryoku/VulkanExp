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
	obj["pbrMetallicRoughness"]["baseColorFactor"] = toJSON(glm::vec4(mat.properties.baseColorFactor, 1.0)); // Saved as vec4 to match glTF
	obj["pbrMetallicRoughness"]["metallicFactor"] = mat.properties.metallicFactor;
	obj["pbrMetallicRoughness"]["roughnessFactor"] = mat.properties.roughnessFactor;
	auto baseColorTexture = JSON::object();
	baseColorTexture["index"] = mat.properties.albedoTexture;
	obj["pbrMetallicRoughness"]["baseColorTexture"] = baseColorTexture;
	auto metallicRoughnessTexture = JSON::object();
	metallicRoughnessTexture["index"] = mat.properties.metallicRoughnessTexture;
	obj["pbrMetallicRoughness"]["metallicRoughnessTexture"] = metallicRoughnessTexture;
	auto normalTexture = JSON::object();
	normalTexture["index"] = mat.properties.normalTexture;
	obj["normalTexture"] = normalTexture;
	obj["emissiveFactor"] = toJSON(mat.properties.emissiveFactor);
	auto emissiveTexture = JSON::object();
	emissiveTexture["index"] = mat.properties.emissiveTexture;
	obj["emissiveTexture"] = emissiveTexture;

	return obj;
}
