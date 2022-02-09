#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

struct KeyboardShortcut
{
	int key;
	int action = GLFW_PRESS;
	int mods = 0;
	
	bool operator==(const KeyboardShortcut& o) const
	{
		return key == o.key && action == o.action && (mods == o.mods || (mods & o.mods));
	}
};
	
namespace std
{
	template <class T>
	inline void hash_combine(std::size_t & seed, const T & v)
	{
		std::hash<T> hasher;
		seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}
	
	template <> struct hash<KeyboardShortcut>
	{
		size_t operator()(const KeyboardShortcut & x) const
		{
			std::size_t seed = 0;
			hash_combine(seed, x.key);
			hash_combine(seed, x.action);
			hash_combine(seed, x.mods);
			return seed;
		}
	};
}
