#pragma once

#include <array>
#include <cassert>
#include <string_view>
#include <vector>

char decodeBase64(char c) {
	if(c >= 'A' && c <= 'Z')
		return c - 'A';
	if(c >= 'a' && c <= 'z')
		return 26 + c - 'a';
	if(c >= '0' && c <= '9')
		return 52 + c - '0';
	if(c == '+')
		return 62;
	if(c == '/')
		return 63;
	assert(false);
	return 0;
}

std::vector<char> decodeBase64(const std::string_view& str) {
	assert(str.size() % 4 == 0); // We only support padded string for now.
	std::vector<char> result;
	result.reserve(str.size() / 4 * 3);
	for(size_t i = 0; i < str.size() / 4 - 1; ++i) {
		std::array<char, 4> d{
			decodeBase64(str[4 * i + 0]),
			decodeBase64(str[4 * i + 1]),
			decodeBase64(str[4 * i + 2]),
			decodeBase64(str[4 * i + 3]),
		};
		int32_t data = (d[0] << 18) | (d[1] << 12) | (d[2] << 6) | d[3];
		result.push_back(static_cast<char>((data >> 2 * 8) & 0xFF));
		result.push_back(static_cast<char>((data >> 1 * 8) & 0xFF));
		result.push_back(static_cast<char>((data >> 0 * 8) & 0xFF));
	}
	// Last quadruplet may contain padding
	if(str.size() % 4 != 0) {
		// TODO
	} else {
		const auto i = str.size() / 4 - 1;
		// May contain padding character(s)
		if(str[str.size() - 1] == '=') {
			if(str[str.size() - 2] == '=') {
				std::array<char, 2> d{
					decodeBase64(str[4 * i + 0]),
					decodeBase64(str[4 * i + 1]),
				};
				int32_t data = (d[0] << 18) | ((d[1] << 12) & 0b110000);
				result.push_back(static_cast<char>((data >> 2 * 8) & 0xFF));
			} else {
				std::array<char, 3> d{
					decodeBase64(str[4 * i + 0]),
					decodeBase64(str[4 * i + 1]),
					decodeBase64(str[4 * i + 2]),
				};
				int32_t data = (d[0] << 18) | (d[1] << 12) | ((d[2] << 6) & 0b111100);
				result.push_back(static_cast<char>((data >> 2 * 8) & 0xFF));
				result.push_back(static_cast<char>((data >> 1 * 8) & 0xFF));
			}
		} else {
			std::array<char, 4> d{
				decodeBase64(str[4 * i + 0]),
				decodeBase64(str[4 * i + 1]),
				decodeBase64(str[4 * i + 2]),
				decodeBase64(str[4 * i + 3]),
			};
			int32_t data = (d[0] << 18) | (d[1] << 12) | (d[2] << 6) | d[3];
			result.push_back(static_cast<char>((data >> 2 * 8) & 0xFF));
			result.push_back(static_cast<char>((data >> 1 * 8) & 0xFF));
			result.push_back(static_cast<char>((data >> 0 * 8) & 0xFF));
		}
	}
	return result;
}
