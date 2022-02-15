#include "JSON.hpp"

#include <stack>

#include <Logger.hpp>

JSON::JSON(const std::filesystem::path& path) {
	parse(path);
}

bool JSON::parse(const std::filesystem::path& path) {
	std::ifstream file{path, std::ios::in};
	if(!file) {
		error("JSON Parsing error: Could not open ''{}'.\n", path);
		return false;
	}
	return parse(file);
}

bool JSON::parse(std::istream& file) {
	char byte;
	byte = skipWhitespace(file);
	if(byte == '{')
		_root = value{parseObject(file)};
	else if(byte == '[')
		_root = value{parseArray(file)};
	else {
		error("JSON::parse error: Expected '{{' or '[', got '{}'\n", byte);
		return false;
	}

	return true;
}

// NOTE: Used to construct an istream from a arbitrary memory buffer.
template<typename char_type>
struct istreambuf : public std::basic_streambuf<char_type, std::char_traits<char_type>> {
	istreambuf(char_type* buffer, std::streamsize bufferLength) {
		// Set the "get" pointer to the start of the buffer, the next item, and record its length.
		this->setg(buffer, buffer, buffer + bufferLength);
		// set the "put" pointer the start of the buffer and record it's length.
		this->setp(buffer, buffer + bufferLength);
	}
};

bool JSON::parse(char* data, size_t size) {
	/* NOTE: Workaround
	I'd like to do the following, but pubsetbuf is not implemented ('correctly'?) in Visual Studio. Yay.
		std::stringstream stream;
		stream.rdbuf()->pubsetbuf(jsonChunk.data, jsonChunk.length);
	*/
	istreambuf<char> streambuf(data, size);
	std::istream	 stream(&streambuf);
	return parse(stream);
}

bool JSON::expectImmediate(char c, std::istream& file) {
	char byte;
	file.get(byte);
	if(byte != c) {
		error("JSON::expectImmediate error: Expected '{}', got '{}'\n", c, byte);
		return false;
	}
	return true;
}

bool JSON::expect(char c, std::istream& file) {
	char byte = skipWhitespace(file);
	if(byte != c) {
		error("JSON::expect error: Expected '{}', got '{}'\n", c, byte);
		return false;
	}
	return true;
}

JSON::object JSON::parseObject(std::istream& file) {
	object o;
	while(file) {
		char byte = skipWhitespace(file);
		switch(byte) {
			case '}': return o; break;
			case '"': {
				auto key = parseString(file);
				expect(':', file);
				o.emplace(std::move(key), parseValue(file));
				break;
			}
			case ',':
				// Just continue to the next key/value pair, or the end of the object
				break;
			default: error("JSON::parseObject error: Unexpected character '{}'.\n", byte); return o;
		}
	}
	return o;
}

JSON::array JSON::parseArray(std::istream& file) {
	array a;
	while(file) {
		char byte = skipWhitespace(file);
		switch(byte) {
			case ']': return a;
			case ',': break;
			default:
				file.putback(byte);
				a.emplace_back(parseValue(file));
				break;
		}
	}
	return a;
}

JSON::string JSON::parseString(std::istream& file) {
	// We assume the leading '"' has already been consumed.
	string s;
	bool   escapeNext = false;

	while(file) {
		char byte = file.get();
		switch(byte) {
			case '"':
				if(escapeNext) {
					s += '"';
					escapeNext = false;
				} else
					return s;
				break;
			case '\\':
				if(escapeNext) {
					s += '\\';
					escapeNext = false;
				} else
					escapeNext = true;
				break;
			default:
				if(escapeNext) {
					switch(byte) {
						case '"': s += '"'; break;
						case '\\': s += '\\'; break;
						case '/': s += '/'; break;
						case 'b': s += '\b'; break;
						case 'f': s += '\f'; break;
						case 'n': s += '\n'; break;
						case 'r': s += '\r'; break;
						case 't': s += '\t'; break;
						case 'u':
							// TODO: Handle unicode codepoint (\u followed by 4 hex digits)
							assert(false);
							break;
					}
					escapeNext = false;
				} else {
					s += byte;
				}
				break;
		}
	}
	return s;
}

JSON::number JSON::parseNumber(std::istream& file) {
	char   byte;
	char   buffer[32];
	size_t size = 0;
	bool   isFloat = false;
	file.get(byte);
	do {
		assert(size < 32);
		buffer[size] = byte;
		++size;
		if(byte == '.' || byte == 'e' || byte == 'E')
			isFloat = true;
		file.get(byte);
	} while(file && (byte == '-' || byte == '+' || (byte >= '0' && byte <= '9') || byte == '.' || byte == 'e' || byte == 'E'));
	if(file) // We've gone past the number
		file.putback(byte);
	if(isFloat) {
		float f;
		std::from_chars(buffer + 0, buffer + size, f);
		return number{f};
	} else {
		int i;
		std::from_chars(buffer + 0, buffer + size, i);
		return number{i};
	}
}

bool JSON::parseBoolean(std::istream& file) {
	char byte;
	file.get(byte);
	if(byte == 't') {
		expectImmediate('r', file);
		expectImmediate('u', file);
		expectImmediate('e', file);
		return true;
	} else if(byte == 'f') {
		expectImmediate('a', file);
		expectImmediate('l', file);
		expectImmediate('s', file);
		expectImmediate('e', file);
		return false;
	}
	error("JSON::parseBoolean error: Unexpected character '{}'.\n", byte);
	return false;
}

JSON::null_t JSON::parseNull(std::istream& file) {
	expectImmediate('n', file);
	expectImmediate('u', file);
	expectImmediate('l', file);
	expectImmediate('l', file);
	return JSON::null_t{};
}

JSON::value JSON::parseValue(std::istream& file) {
	char byte = skipWhitespace(file);
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
			file.putback(byte);
			return value{parseNumber(file)};
			break;
		case '{': return value{parseObject(file)}; break;
		case '[': return value{parseArray(file)}; break;
		case 't':
		case 'f':
			file.putback(byte);
			return value{parseBoolean(file)};
			break;
		case 'n':
			file.putback(byte);
			return value{parseNull(file)};
			break;
	}
	error("JSON::parseValue: Unexpected character '{}'.\n", byte);
}

bool JSON::save(const std::filesystem::path& path) const {
	std::ofstream file(path);
	if(!file)
		return false;
	save(file);
	return true;
}

bool JSON::save(std::ostream& file) const {
	file << *this;
	return true;
}
