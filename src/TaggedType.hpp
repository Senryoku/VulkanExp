#pragma once

template<typename T, typename Tag>
struct TaggedType {
	using UnderlyingType = T;

	TaggedType() = default;
	TaggedType(const TaggedType&) = default;
	TaggedType(TaggedType&&) = default;

	explicit TaggedType(const T& value = T{}) : value(value) {}
	explicit TaggedType(T&& value) : value(std::move(value)) {}

	TaggedType& operator=(const TaggedType&) = default;
	friend auto operator<=>(const TaggedType&, const TaggedType&) = default;
				operator T() const { return value; }
	// explicit	operator T() const { return value; }
	T value;
};

template<typename T, typename Tag>
struct TaggedIndex : public TaggedType<T, Tag> {
	TaggedIndex() = default;
	TaggedIndex(const TaggedIndex&) = default;
	TaggedIndex(TaggedIndex&&) = default;
	explicit TaggedIndex(const T& value = T{}) : TaggedType<T, Tag>(value) {}
	explicit TaggedIndex(T&& value) : TaggedType<T, Tag>(value) {}
	TaggedIndex& operator=(const TaggedIndex&) = default;
	T			 operator++() { return this->value++; }
	T&			 operator++(int) { return ++this->value; }
};
