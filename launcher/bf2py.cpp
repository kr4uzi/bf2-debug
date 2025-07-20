#include "bf2py.h"
#ifdef _WIN32
#include <Windows.h>
#include <memory>
#include <iostream>
#include <format>
#endif

#ifdef _WIN32
std::wstring error_message_from_error_code(DWORD errorCode)
{
	wchar_t* messageBuffer = nullptr;
	auto msg = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
	if (msg > 0) {
		auto deleter = std::unique_ptr<wchar_t, void(*)(wchar_t *)>{
			messageBuffer,
			[](auto buff) { ::LocalFree(buff); }
		};

		return messageBuffer;
	}

	return std::format(L"Failed to retrieve error message for code {}", errorCode);
}
#endif

std::string bf2_get_python_version(const std::filesystem::path& bf2Path)
{
#ifdef _WIN32
	if (!::SetDllDirectoryW(bf2Path.c_str())) {
		auto errorCode = ::GetLastError();
		std::wcerr << "Failed to set DLL directory to " << bf2Path << ": " << error_message_from_error_code(errorCode) << std::endl;
		return {};
	}

	auto restoreDllDirectory = std::unique_ptr<void, void(*)(void*)>(
		nullptr, [](void*) { ::SetDllDirectoryW(nullptr); }
	);

	auto dicePyPath = bf2Path / L"dice_py.dll";
	auto handle = ::LoadLibraryW(dicePyPath.c_str());
	if (!handle) {
		auto errorCode = ::GetLastError();
		std::wcerr << "Failed to load Python DLL from " << dicePyPath << ": " << error_message_from_error_code(errorCode) << std::endl;

		return {};
	}

	auto freeHandle = std::unique_ptr<decltype(handle), void(*)(decltype(handle)*)>(
		&handle, [](HMODULE* hndl) { ::FreeLibrary(*hndl); }
	);
	auto pyGetVersion = reinterpret_cast<const char* (*)()>(::GetProcAddress(handle, "Py_GetVersion"));
	if (pyGetVersion) {
		return pyGetVersion();
	}
#else
#error "bf2_python_get_version is only implemented for Windows"
#endif

	return {};
}
