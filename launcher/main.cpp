#include "bf2launch.h"
#include "bf2reg.h"
#include <filesystem>
#include <print>

int main(int argc, char* argv[])
{
	std::string bf2Path;
	std::vector<std::string> dlls;
	std::vector<std::string> otherArgs;
	for (int i = 1; i < argc; i++) {
		auto arg = std::string{ argv[i] };
		if (arg.starts_with("-inject=")) {
			dlls.push_back(arg.substr(8));
		}
		else if (arg.starts_with("-bf2path=")) {
			bf2Path = arg.substr(9);
		}
		else {
			otherArgs.push_back(arg);
		}
	}

	if (bf2Path.empty() && !std::filesystem::exists("bf2_w32ded.exe")) {
		std::println("bf2_w32ded.exe does not exist in working directory, retrieving path from registry ...");
		bf2Path = bf2_dir_from_registry();
		if (bf2Path.empty()) {
			std::println("Battlefield 2 (Server) installation could not be detected");
			std::println("Please copy this file to the Battlefield 2 (Server) directory and try again.");
			return 1;
		}

		std::println("starting bf2 from {}", bf2Path);
	}

	run_bf2(argv[0], dlls, bf2Path, otherArgs);

	return 0;
}