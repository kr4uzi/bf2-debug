#include "bf2simulator.h"
#include <csignal>
#include <memory>

bf2py::bf2simulator simulator;
void signal_handler(int signum) {
	simulator.stop();
}

int main(int argc, char* argv[]) {
	std::vector<std::string> dlls;
	for (int i = 1; i < argc; i++) {
		auto arg = std::string{ argv[i] };
		if (arg.starts_with("-inject=")) {
			dlls.push_back(arg.substr(8));
		}
	}

	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);

	simulator.dlls(dlls);
	return simulator.run();
}
