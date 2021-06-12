#pragma once

#include <string>
#include <string_view>
#include <vector>

std::vector<std::string_view> split(const std::string& str, const std::string& delimiters) {
    std::vector<std::string_view> r;

    size_t start = 0;
    size_t end = 0;
    while((end = str.find_first_of(delimiters, start)) != std::string::npos) {
        if(end - start > 0)
            r.push_back({str.c_str() + start, end - start});
        start = end + 1;
    }
    if(str.size() - start > 0)
        r.push_back({str.c_str() + start, str.size() - start});
    return r;
}