#include "Application.hpp"
#include <fmt/core.h>

int main() {
	Application app;

	// try {
	app.run();
	//} catch(const std::exception& e) {
	//	error("Uncaught exception: {}\n", e.what());
	//	return EXIT_FAILURE;
	//}

	return EXIT_SUCCESS;
}
