#include "JSON.hpp"

#include <stack>

#include <Logger.hpp>

JSON::JSON(const std::filesystem::path& path) {
	parse(path);
}

bool JSON::parse(const std::filesystem::path& path) {
	std::ifstream file{path, std::ios::in};
	return parse(file);
}

bool JSON::parse(std::ifstream& file) {
	char byte;
	byte = skipWhitespace(file);
	if(byte == '{')
		_root = value{parseObject(file)};
	else if(byte == '[')
		_root = value{parseArray(file)};
	else {
		error("JSON Parsing error: Expected '\\{' or '[', got '{}'\n", byte);
	}

	return true;
}

bool JSON::expect(char c, std::ifstream& file) {
	char byte = skipWhitespace(file);
	if(byte != c) {
		error("JSON Parsing error: Expected '{}', got '{}'\n", c, byte);
		return false;
	}
	return true;
}

JSON::object JSON::parseObject(std::ifstream& file) {
	object o;
	while(file) {
		char byte = skipWhitespace(file);
		switch(byte) {
			case '}': return o; break;
			case '"': {
				auto key = parseString(file);
				expect(':', file);
				auto value = parseValue(file);
				o[key] = value;
				break;
			}
			case ',':
				// Just continue to the next key/value pair, or the end of the object
				break;
			default: error("JSON Parsing error: Unexpected character '{}'.\n", byte); return o;
		}
	}
	return o;
}

JSON::array JSON::parseArray(std::ifstream& file) {
	array a;
	while(file) {
		char byte = skipWhitespace(file);
		switch(byte) {
			case ']': return a;
			case ',': break;
			default:
				file.seekg(-1, file.cur);
				a.push(parseValue(file));
				break;
		}
	}
	return a;
}

JSON::string JSON::parseString(std::ifstream& file) {
	// We assume the leading '"' has already been consumed.
	string s;
	bool   escapeNext = false;
	while(file) {
		char byte = skipWhitespace(file);
		switch(byte) {
			case '"':
				if(escapeNext) {
					s += '"';
					escapeNext = false;
				} else
					return s;
				break;
			case '\\':
				if(escapeNext)
					s += '\\';
				else
					escapeNext = true;
				break;
			default:
				if(escapeNext) {
					// TODO: Correctly handle escaped caracters.
					escapeNext = false;
				} else {
					s += byte;
				}
				break;
		}
	}
	return s;
}

JSON::number JSON::parseNumber(std::ifstream& file) {
	char		byte;
	std::string str;
	bool		isFloat = false;
	do {
		file.get(byte);
		if(byte == '.' || byte == 'e' || byte == 'E')
			isFloat = true;
		str += byte;
	} while(file && (byte == '-' || byte == '+' || (byte >= '0' && byte <= '9') || byte == '.' || byte == 'e' || byte == 'E'));
	if(isFloat) {
		return number{std::stof(str)};
	} else {
		return number{std::stoi(str)};
	}
}

bool JSON::parseBoolean(std::ifstream& file) {
	char byte;
	file.get(byte);
	if(byte == 't') {
		expect('r', file);
		expect('u', file);
		expect('e', file);
		return true;
	} else if(byte == 'f') {
		expect('a', file);
		expect('l', file);
		expect('s', file);
		expect('e', file);
		return false;
	}
	error("JSON Parsing error: Unexpected character '{}'.\n", byte);
	return false;
}

JSON::null_t JSON::parseNull(std::ifstream& file) {
	expect('n', file);
	expect('u', file);
	expect('l', file);
	expect('l', file);
	return JSON::null_t{};
}

JSON::value JSON::parseValue(std::ifstream& file) {
	value v;
	char  byte = skipWhitespace(file);
	switch(byte) {
		case '"': return value{parseString(file)}; break;
		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			file.seekg(-1, file.cur);
			return value{parseNumber(file)};
			break;
		case '{': return value{parseObject(file)}; break;
		case '[': return value{parseArray(file)}; break;
		case 't':
		case 'f':
			file.seekg(-1, file.cur);
			return value{parseBoolean(file)};
			break;
		case 'n':
			file.seekg(-1, file.cur);
			return value{parseNull(file)};
			break;
	}
	return v;
}
