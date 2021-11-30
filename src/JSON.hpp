#pragma once

#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <fmt/ostream.h>

class JSON {
  public:
	class value;

	class null_t {};

	class number {
	  public:
		number() = default;
		number(const number&) = default;
		number(number&&) = default;
		number(int i) : _type(Type::integer) { _value.as_int = i; }
		number(float f) : _type(Type::real) { _value.as_float = f; }

		std::string toString() const {
			if(_type == Type::real)
				return std::to_string(_value.as_float);
			else
				return std::to_string(_value.as_int);
		}

		const float& asReal() const {
			assert(_type == Type::real);
			return _value.as_float;
		}

		const int& asInteger() const {
			assert(_type == Type::integer);
			return _value.as_int;
		}

		operator const float&() const { return asReal(); }
		operator const int&() const { return asInteger(); }

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

	using string = std::string;
	using array = std::vector<value>;
	using object = std::unordered_map<string, value>;
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
		value(value& v) { copy(v); };
		value(const value& v) noexcept { copy(v); };
		value& operator=(const value& v) noexcept {
			copy(v);
			return *this;
		}
		value(value&& v) noexcept { swap(std::move(v)); };
		value& operator=(value&& v) noexcept {
			swap(std::move(v));
			return *this;
		}

		value(const object& o) {
			_type = Type::object;
			new(&_value.as_object) object{o};
		};
		value(object&& o) {
			_type = Type::object;
			new(&_value.as_object) object{std::move(o)};
		};
		value(const array& a) {
			_type = Type::array;
			new(&_value.as_array) array{a};
		}
		value(array&& a) {
			_type = Type::array;
			new(&_value.as_array) array{std::move(a)};
		};
		value(const string& s) {
			_type = Type::string;
			new(&_value.as_string) string{s};
		};
		value(string&& s) {
			_type = Type::string;
			new(&_value.as_string) string{std::move(s)};
		};
		value(const number& n) {
			_type = Type::number;
			new(&_value.as_number) number{n};
		};
		value(number&& n) {
			_type = Type::number;
			new(&_value.as_number) number{std::move(n)};
		};
		value(bool b) {
			_type = Type::boolean;
			_value.as_boolean = b;
		};
		value(null_t n) {
			_type = Type::null;
			new(&_value.as_null) null_t{n};
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

		auto operator<=>(const value&) const = default;

		std::string toString() const {
			switch(_type) {
				case Type::string: return _value.as_string;
				case Type::number: return _value.as_number.toString();
				case Type::array: return JSON::toString(_value.as_array);
				case Type::object: return JSON::toString(_value.as_object);
				case Type::boolean: return _value.as_boolean ? "true" : "false";
				case Type::null: return "null";
			}
		}

		Type		  getType() const { return _type; }
		const object& asObject() const {
			assert(_type == Type::object);
			return _value.as_object;
		}
		const array& asArray() const {
			assert(_type == Type::array);
			return _value.as_array;
		}
		const number& asNumber() const {
			assert(_type == Type::number);
			return _value.as_number;
		}
		const string& asString() const {
			assert(_type == Type::string);
			return _value.as_string;
		}

		template<class T>
		const T& as() const;

		template<>
		const string& as<string>() const {
			return asString();
		}
		template<>
		const int& as<int>() const {
			return asNumber().asInteger();
		}
		template<>
		const float& as<float>() const {
			return asNumber().asReal();
		}

		class iterator {
		  public:
			using iterator_category = std::forward_iterator_tag;
			using difference_type = std::ptrdiff_t;
			using value_type = value;
			using pointer = value_type*;
			using reference = value_type&;

			iterator(const iterator& it) : _type{it._type} {
				if(_type == Type::array)
					_it.array_it = it._it.array_it;
				if(_type == Type::object)
					_it.object_it = it._it.object_it;
			}

			iterator(array::iterator it) {
				_type = Type::array;
				_it.array_it = it;
			}
			iterator(object::iterator it) {
				_type = Type::object;
				_it.object_it = it;
			}

			reference operator*() {
				if(_type == Type::array)
					return *_it.array_it;
				if(_type == Type::object)
					return _it.object_it->second;
				assert(false);
				return *_it.array_it;
			}

			reference operator->() {
				if(_type == Type::array)
					return *_it.array_it;
				if(_type == Type::object)
					return _it.object_it->second;
				assert(false);
				return *_it.array_it;
			}

			// Prefix increment
			iterator& operator++() {
				if(_type == Type::array)
					++_it.array_it;
				if(_type == Type::object)
					++_it.object_it;
				return *this;
			}

			// Postfix increment
			iterator operator++(int) {
				auto tmp = *this;
				++(*this);
				return tmp;
			}

			friend bool operator==(const iterator& a, const iterator& b) {
				if(a._type != b._type)
					return false;
				if(a._type == Type::array)
					return a._it.array_it == b._it.array_it;
				if(a._type == Type::object)
					return a._it.object_it == b._it.object_it;
				assert(false);
			};
			friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); };

		  private:
			union IteratorUnion {
				IteratorUnion() {}
				~IteratorUnion() {}
				array::iterator	 array_it;
				object::iterator object_it;
			};
			IteratorUnion _it;
			Type		  _type;
		};

		class const_iterator {
		  public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = value;
			using reference = const value_type&;

			const_iterator(const const_iterator& it) : _type{it._type} {
				if(_type == Type::array)
					_it.array_it = it._it.array_it;
				if(_type == Type::object)
					_it.object_it = it._it.object_it;
			}

			const_iterator(array::const_iterator it) {
				_type = Type::array;
				_it.array_it = it;
			}
			const_iterator(object::const_iterator it) {
				_type = Type::object;
				_it.object_it = it;
			}

			reference operator*() {
				if(_type == Type::array)
					return *_it.array_it;
				if(_type == Type::object)
					return _it.object_it
						->second; // FIXME: This is probably not what you'd expect (but the return type has to match, we could encapsule the key in a value, but em...)
				assert(false);
				return *_it.array_it;
			}

			reference operator->() {
				assert(_type == Type::array || _type == Type::object);
				if(_type == Type::array)
					return *_it.array_it;
				if(_type == Type::object)
					return _it.object_it->second;
				return *_it.array_it;
			}

			const_iterator& operator++() {
				assert(_type == Type::array || _type == Type::object);
				if(_type == Type::array)
					++_it.array_it;
				if(_type == Type::object)
					++_it.object_it;
				return *this;
			}

			const_iterator operator++(int) {
				auto tmp = *this;
				++(*this);
				return tmp;
			}

			friend bool operator==(const const_iterator& a, const const_iterator& b) {
				if(a._type != b._type)
					return false;
				if(a._type == Type::array)
					return a._it.array_it == b._it.array_it;
				if(a._type == Type::object)
					return a._it.object_it == b._it.object_it;
				return false;
			};
			friend bool operator!=(const const_iterator& a, const const_iterator& b) { return !(a == b); };

		  private:
			union IteratorUnion {
				IteratorUnion(){};
				~IteratorUnion(){};
				array::const_iterator  array_it{};
				object::const_iterator object_it;
			};
			IteratorUnion _it;
			Type		  _type;
		};

		iterator begin() noexcept {
			if(_type == Type::array)
				return _value.as_array.begin();
			if(_type == Type::object)
				return _value.as_object.begin();
			assert(false);
			return _value.as_array.begin();
		}

		const_iterator begin() const noexcept {
			if(_type == Type::array)
				return _value.as_array.cbegin();
			if(_type == Type::object)
				return _value.as_object.cbegin();
			assert(false);
			return _value.as_array.cbegin();
		}

		iterator end() noexcept {
			if(_type == Type::array)
				return _value.as_array.end();
			if(_type == Type::object)
				return _value.as_object.end();
			assert(false);
			return _value.as_array.end();
		}

		const_iterator end() const noexcept {
			if(_type == Type::array)
				return _value.as_array.cend();
			if(_type == Type::object)
				return _value.as_object.cend();
			assert(false);
			return _value.as_array.cend();
		}

		const value& operator[](size_t idx) const {
			assert(_type == Type::array);
			return _value.as_array[idx];
		}
		value& operator[](size_t idx) {
			assert(_type == Type::array);
			return _value.as_array[idx];
		}

		const value& operator[](const string& key) const {
			assert(_type == Type::object);
			return _value.as_object.at(key);
		}
		value& operator[](const string& key) {
			assert(_type == Type::object);
			return _value.as_object[key];
		}

	  private:
		union ValueType {
			ValueType(){};
			~ValueType(){};

			string as_string;
			number as_number{0};
			array  as_array;
			object as_object;
			bool   as_boolean;
			null_t as_null;
		};
		Type	  _type = Type::null;
		ValueType _value = {};

		void swap(value&& v) {
			_type = v._type;
			switch(_type) {
				case Type::string: new(&_value.as_string) string{std::move(v._value.as_string)}; break;
				case Type::number: new(&_value.as_number) number{std::move(v._value.as_number)}; break;
				case Type::array: new(&_value.as_array) array{std::move(v._value.as_array)}; break;
				case Type::object: new(&_value.as_object) object{std::move(v._value.as_object)}; break;
				case Type::boolean: _value.as_boolean = v._value.as_boolean; break;
				case Type::null: new(&_value.as_null) null_t{std::move(v._value.as_null)}; break;
			}
		}

		void copy(const value& v) {
			_type = v._type;
			switch(_type) {
				case Type::string: new(&_value.as_string) string{v._value.as_string}; break;
				case Type::number: new(&_value.as_number) number{v._value.as_number}; break;
				case Type::array: new(&_value.as_array) array{v._value.as_array}; break;
				case Type::object: new(&_value.as_object) object{v._value.as_object}; break;
				case Type::boolean: _value.as_boolean = v._value.as_boolean; break;
				case Type::null: new(&_value.as_null) null_t{v._value.as_null}; break;
			}
		}
	};

	static inline std::string toString(const array& value) {
		std::string s;
		s += '[';
		for(const auto& v : value)
			s += v.toString() + ", ";
		s += ']';
		return s;
	}

	static inline std::string toString(const object& value) {
		std::string s;
		s += '{';
		for(const auto& v : value)
			s += v.first + ": " + v.second.toString() + ", ";
		s += '}';
		return s;
	}

	JSON(const std::filesystem::path&);

	bool parse(const std::filesystem::path&);
	bool parse(std::ifstream&);

	const value& getRoot() const { return _root; };

  private:
	inline static bool isWhitespace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
	inline static char skipWhitespace(std::ifstream& file) {
		char byte;
		do
			file.get(byte);
		while(file && isWhitespace(byte));
		return byte;
	}

	static object parseObject(std::ifstream&);
	static array  parseArray(std::ifstream&);
	static string parseString(std::ifstream&);
	static number parseNumber(std::ifstream&);
	static bool	  parseBoolean(std::ifstream&);
	static null_t parseNull(std::ifstream&);
	static value  parseValue(std::ifstream&);

	static bool expectImmediate(char c, std::ifstream&);
	static bool expect(char c, std::ifstream&);

	value _root;
};

inline std::ostream& operator<<(std::ostream& os, const JSON::value& value) {
	return os << value.toString();
}

inline std::ostream& operator<<(std::ostream& os, const JSON& json) {
	return os << json.getRoot();
}