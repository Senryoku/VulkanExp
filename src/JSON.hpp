#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <vector>

class JSON {
  public:
	class value;

	class null_t {
	  public:
		null_t() = default;
	};
	class number {
	  public:
		number() = default;
		number(int i) : _type(Type::integer) { _value.as_int = i; }
		number(float f) : _type(Type::real) { _value.as_float = f; }

	  private:
		enum class Type
		{
			real,
			integer
		};
		Type _type = Type::integer;
		union {
			int	  as_int = 0;
			float as_float;
		} _value;
	};
	class string {
	  public:
		string& operator+=(char c) {
			_str += c;
			return *this;
		}
		bool operator==(const string& s) const { return _str == s._str; }

		struct Hasher {
			std::size_t operator()(const JSON::string& k) const { return k.hash(); }
		};
		std::size_t hash() const { return std::hash<std::string>{}(_str); }

	  private:
		std::string _str;
	};
	class array {
	  public:
		void push(const value& v) { _values.push_back(v); }

	  private:
		std::vector<value> _values;
	};
	class object {
	  public:
		value& operator[](const string& key) { return _map[key]; }

	  private:
		std::unordered_map<string, value, string::Hasher> _map;
	};
	class value {
	  public:
		enum class Type
		{
			object,
			array,
			string,
			number,
			boolean,
			null
		};

		value(){};
		value(value& v) : _type(v._type) {
			switch(_type) {
				case Type::string: _value.as_string = v._value.as_string; break;
				case Type::number: _value.as_number = v._value.as_number; break;
				case Type::array: _value.as_array = v._value.as_array; break;
				case Type::object: _value.as_object = v._value.as_object; break;
				case Type::boolean: _value.as_boolean = v._value.as_boolean; break;
				case Type::null: _value.as_null = v._value.as_null; break;
			}
		};
		value(const value& v) noexcept : _type(v._type) {
			switch(_type) {
				case Type::string: _value.as_string = v._value.as_string; break;
				case Type::number: _value.as_number = v._value.as_number; break;
				case Type::array: _value.as_array = v._value.as_array; break;
				case Type::object: _value.as_object = v._value.as_object; break;
				case Type::boolean: _value.as_boolean = v._value.as_boolean; break;
				case Type::null: _value.as_null = v._value.as_null; break;
			}
		}; /*
value(value&& v) { *this = v; };
value& operator=(value&& v) {
_type = v._type;
switch(_type) {
case Type::string: _value.as_string = std::move(v._value.as_string); break;
case Type::number: _value.as_number = std::move(v._value.as_number); break;
case Type::array: _value.as_array = std::move(v._value.as_array); break;
case Type::object: _value.as_object = std::move(v._value.as_object); break;
case Type::boolean: _value.as_boolean = std::move(v._value.as_boolean); break;
case Type::null: _value.as_null = std::move(v._value.as_null); break;
}
return *this;
}*/
		value& operator=(const value& v) noexcept {
			_type = v._type;
			switch(_type) {
				case Type::string: _value.as_string = v._value.as_string; break;
				case Type::number: _value.as_number = v._value.as_number; break;
				case Type::array: _value.as_array = v._value.as_array; break;
				case Type::object: _value.as_object = v._value.as_object; break;
				case Type::boolean: _value.as_boolean = v._value.as_boolean; break;
				case Type::null: _value.as_null = v._value.as_null; break;
			}
			return *this;
		}
		value& operator=(value& v) {
			_type = v._type;
			switch(_type) {
				case Type::string: _value.as_string = v._value.as_string; break;
				case Type::number: _value.as_number = v._value.as_number; break;
				case Type::array: _value.as_array = v._value.as_array; break;
				case Type::object: _value.as_object = v._value.as_object; break;
				case Type::boolean: _value.as_boolean = v._value.as_boolean; break;
				case Type::null: _value.as_null = v._value.as_null; break;
			}
			return *this;
		}
		auto operator<=>(const value&) const = default;

		value(const object& o) {
			_type = Type::object;
			_value.as_object = o;
		};
		value(const array& a) {
			_type = Type::array;
			_value.as_array = a;
		};
		value(const string& s) {
			_type = Type::string;
			_value.as_string = s;
		};
		value(const number& n) {
			_type = Type::number;
			_value.as_number = n;
		};
		value(bool b) {
			_type = Type::boolean;
			_value.as_boolean = b;
		};
		value(null_t n) {
			_type = Type::null;
			_value.as_null = n;
		};

		~value() {
			switch(_type) {
				case Type::string: _value.as_string.~string(); break;
				case Type::number: _value.as_number.~number(); break;
				case Type::array: _value.as_array.~array(); break;
				case Type::object: _value.as_object.~object(); break;
				case Type::boolean:
				case Type::null: break;
			}
		};

	  private:
		union ValueType {
			ValueType(){};
			~ValueType(){};

			string as_string = {};
			number as_number;
			array  as_array;
			object as_object;
			bool   as_boolean;
			null_t as_null;
		};
		Type	  _type = Type::null;
		ValueType _value = {};
	};

	JSON(const std::filesystem::path&);

	bool parse(const std::filesystem::path&);
	bool parse(std::ifstream&);

  private:
	static bool isWhitespace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
	static char skipWhitespace(std::ifstream& file) {
		char byte = ' ';
		while(file && isWhitespace(byte))
			file.get(byte);
		return byte;
	}

	static object parseObject(std::ifstream&);
	static array  parseArray(std::ifstream&);
	static string parseString(std::ifstream&);
	static number parseNumber(std::ifstream&);
	static bool	  parseBoolean(std::ifstream&);
	static null_t parseNull(std::ifstream&);
	static value  parseValue(std::ifstream&);

	static bool expect(char c, std::ifstream&);

	value _root;
};
