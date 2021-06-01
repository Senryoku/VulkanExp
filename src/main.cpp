#include "Application.hpp"
#include <fmt/core.h>

int main() {
    HelloTriangleApplication app;

    try {
        app.run();
    } catch (const std::exception& e) {
        fmt::print(e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}