#include "bf2launcher.h"
#include "unicode.h"
#include <filesystem>
#include <regex>
#include <print>

int main(int argc, char* argv[])
{
	if (!bf2py::argv_to_utf8(argc, argv)) {
		std::println("failed to convert command line arguments to UTF-8!\nusing non-UTF-8 provided arguments...");
	}

	std::vector<std::filesystem::path> dlls;
	std::vector<std::wstring> bf2Args;
	bool autoInjectDebugDLL = true;
	bool autoDetectPath = true;

	bf2py::launcher launcher;
	launcher.executable(L"bf2_w32ded.exe");

	for (int i = 1; i < argc; i++) {
		auto arg = bf2py::from_utf8(argv[i]);
		if (arg.starts_with(L"-inject=")) {
			auto dllPath = arg.substr(8);
			dlls.push_back(dllPath);

			static auto pattern = std::wregex{ LR"((?:^|.*\\)bf2py(\d+\.\d+\.\d+)?-debug\.dll$)" };
			if (std::regex_match(dllPath, pattern)) {
				autoInjectDebugDLL = false;
			}
		}
		else if (arg.starts_with(L"-bf2path=")) {
			autoDetectPath = false;
			launcher.path(arg.substr(9));
		}
		else {
			bf2Args.push_back(arg);
		}
	}

	if (!bf2Args.empty()) {
		std::wstringstream ss;
		for (const auto& arg : bf2Args) {
			ss << " " << std::quoted(std::wstring{arg.begin(), arg.end()});
		}

		launcher.args(ss.str().substr(1));
	}

	if (autoDetectPath) {
		std::println("Auto-detecting Battlefield 2 (Server) installation path ...");
		if (std::filesystem::exists("bf2_w32ded.exe")) {
			std::println("Found bf2_w32ded.exe in current working directory.");
			launcher.path(std::filesystem::current_path());
		}
		else if (launcher.setPathFromRegistry()) {
			std::println("Battlefield 2 (Server) installation detected in registry");
		} else {
			std::println("Battlefield 2 (Server) installation could not be detected");
			std::println("Please copy this file to the Battlefield 2 (Server) directory and try again.");
			std::println("Alternatively, you can specify the path to the Battlefield 2 (Server) directory using -bf2path=<path>.");
			return 1;
		}
	}

	if (autoInjectDebugDLL) {
		std::println("Trying to auto-inject the bf2py*-debug.dll based on the detected python version ...");
		auto bf2pyVer = launcher.getPythonVersion();
		std::regex version_regex(R"(^(\d+\.\d+\.\d+).*$)");
		std::smatch match;

		if (std::regex_match(bf2pyVer, match, version_regex)) {
			bf2pyVer = match[1];
		}
		else if (bf2pyVer.empty()) {
			std::println("Failed to detect python version - please use -inject=<path-to-bf2py-debug.dll>");
			return 1;
		} else {
			std::println("Failed to detect python version from version string: {}", bf2pyVer);
			std::println("To skip auto-injection, use -inject=<path-to-bf2py-debug.dll>");
			return 1;
		}

		auto bf2pyDebugDll = std::format("bf2py{}-debug.dll", bf2pyVer);
		if (!std::filesystem::exists(bf2pyDebugDll)) {
			std::println("bf2py{}-debug.dll was not found in working directory.", bf2pyVer);
			std::println("If you use a non-standard python version, you need a custom build of bf2py-debug");
			return 1;
		}

		dlls.push_back(bf2pyDebugDll);
	}

	launcher.dlls(dlls);
	launcher.run();
	return 0;
}