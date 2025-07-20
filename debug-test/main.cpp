#include "usage.h"
#include "drylaunch.h"
#include "bf2reg.h"
#include <vector>
#include <string>
#include <print>
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
	std::string bf2Path;
	std::vector<std::string> dlls;
	std::vector<std::string> otherArgs;
	for (int i = 1; i < argc; i++) {
		auto arg = std::string{ argv[i] };
		if (arg.starts_with("-inject=")) {
			dlls.push_back(arg.substr(8));
		}
	}	

	run_dry(argv[0], dlls);

	return 0;
}
