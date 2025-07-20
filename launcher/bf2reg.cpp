#include "bf2reg.h"
#ifdef _WIN32
#include <map>
#include <print>
#include <vector>
#include <Windows.h>
#endif

std::wstring bf2_dir_from_registry()
{
#ifdef _WIN32
	std::map<std::wstring, std::wstring> regPaths = {
		{ LR"(SOFTWARE\EA GAMES\Battlefield 2 Server)", L"GAMEDIR" },
		{ LR"(SOFTWARE\Electronic Arts\EA Games\Battlefield 2)", L"InstallDir"},
	};

	for (const auto& [path, key] : regPaths) {
		DWORD size = 0;
		auto res = RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), key.c_str(), RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, nullptr, &size);
		if (res != ERROR_SUCCESS) {
			continue;
		}

		std::vector<char> buffer(size);
		res = RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), key.c_str(), RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buffer.data(), &size);
		if (res != ERROR_SUCCESS) {
			continue;
		}

		// REG_SZ is zero-terminated
		return reinterpret_cast<wchar_t*>(buffer.data());
	}

	return {};
#else
	return {};
#endif
}