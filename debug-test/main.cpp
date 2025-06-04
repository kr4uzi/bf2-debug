#include "usage.h"
#include "drylaunch.h"
#include "bf2reg.h"
#include <vector>
#include <string>
#include <print>
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
	std::string mode;
	std::string bf2Path;
	std::vector<std::string> dlls;
	std::vector<std::string> otherArgs;
	for (int i = 1; i < argc; i++) {
		auto arg = std::string{ argv[i] };
		if (arg.starts_with("-inject=")) {
			dlls.push_back(arg.substr(8));
		}
		else if (arg.starts_with("-mode=")) {
			mode = arg.substr(6);
		}
		else if (arg.starts_with("-bf2path=")) {
			bf2Path = arg.substr(9);
		}
		else {
			otherArgs.push_back(arg);
		}
	}	

	if (mode.empty()) {
		print_usage(argv[0]);
		std::print("mode: ");
		std::cin >> mode;
	}

	if (mode != "dry" && mode != "bf2") {
		std::println("invalid mode: {}", mode);
		print_usage(argv[0]);
		return 1;
	}

	if (mode == "dry") {
		run_dry(argv[0], dlls);
	}
	else if (mode == "bf2") {
		if (bf2Path.empty() && !std::filesystem::exists("bf2_w32ded.exe")) {
			std::println("bf2_w32ded.exe does not exist in working directory, retrieving path from registry ...");
			bf2Path = bf2_dir_from_registry();
			if (bf2Path.empty()) {
				std::println("Battlefield 2 (Server) installation could not be detected");
				std::println("Please copy this file to the Battlefield 2 (Server) directory and try again.");
				return 1;
			}
		}

		//run_bf2(argv[0], dlls, bf2Path, otherArgs);
	}

	return 0;
}
