#include "bf2reg.h"
#include <map>
#include <print>
#include <Windows.h>

std::string bf2_dir_from_registry()
{
	std::map<std::string, std::string> regPaths = {
		{ R"(SOFTWARE\EA GAMES\Battlefield 2 Server)", "GAMEDIR" },
		{ R"(SOFTWARE\Electronic Arts\EA Games\Battlefield 2)", "InstallDir"},
	};

	for (const auto& [path, key] : regPaths) {
		HKEY hKey = nullptr;
		auto res = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_WOW64_32KEY | KEY_QUERY_VALUE, &hKey);
		if (res != ERROR_SUCCESS) {
			continue;
		}

		if (!hKey) {
			std::println(stderr, "Failed to open {}", path);
			continue;
		}

		BYTE value[MAX_PATH];
		DWORD size = sizeof(value);
		DWORD type = REG_SZ;
		res = RegQueryValueExA(hKey, key.c_str(), nullptr, &type, value, &size);
		if (res != ERROR_SUCCESS) {
			std::println(stderr, "Failed to open {}:{}", path, key);
			continue;
		}

		// note: value might not be null terminated
		return std::string{ value, value + size };
	}

	return "";
}