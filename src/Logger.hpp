#pragma once

#include <fmt/color.h>
#include <fmt/core.h>

template <typename... Args>
void error(Args... args) {
    fmt::print(fg(fmt::color::red), args...);
}

template <typename... Args>
void warn(Args... args) {
    fmt::print(fg(fmt::color::yellow), args...);
}

template <typename... Args>
void success(Args... args) {
    fmt::print(fg(fmt::color::green), args...);
}