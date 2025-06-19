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
		char value[MAX_PATH];
		DWORD size = sizeof(value);
		DWORD type = REG_SZ;
		auto res = RegGetValueA(HKEY_LOCAL_MACHINE, path.c_str(), key.c_str(), RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, value, &size);
		if (res != ERROR_SUCCESS) {
			continue;
		}

		// REG_SZ is zero-terminated
		return value;
	}

	return "";
}