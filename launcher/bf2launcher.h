#pragma once
#include <vector>
#include <filesystem>
#include <string>

namespace bf2py {
	class launcher
	{
		std::wstring _exe;
		std::filesystem::path _path;
		std::wstring _args;
		std::vector<std::filesystem::path> _dlls;
		std::vector<std::string> _normalizedDlls; // used for detour DLLs
		std::vector<const char*> _detourDllPtrs; // pointers to normalizedDlls, used for DetourCreateProcessWithDllsW

	public:
		std::wstring executable() const { return _exe; }
		void executable(const std::wstring& exe) { _exe = exe; }

		std::filesystem::path path() const { return _path; }
		void path(const std::filesystem::path& path) { _path = path; }

		std::wstring args() const { return _args; }
		void args(const std::wstring& args) { _args = args; }

		std::vector<std::filesystem::path> dlls() const { return _dlls; }
		void dlls(const std::vector<std::filesystem::path>& dlls) { _dlls = dlls; }
		bool checkAndNormalizeDLLs();

		bool setPathFromRegistry();

		std::string getPythonVersion() const;

		int run();
	};
}
