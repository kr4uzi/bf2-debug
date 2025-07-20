#include "bf2py.h"
#ifdef _WIN32
#include <Windows.h>
#include <memory>
#endif

std::string bf2_get_python_version(const std::filesystem::path& bf2Path)
{
#ifdef _WIN32
	auto handle = ::LoadLibraryW((bf2Path / L"dice_py.dll").c_str());
	if (!handle) {
		return {};
	}

	auto freeHandle = std::unique_ptr<decltype(handle), void(*)(decltype(handle)*)>(
		&handle, [](auto hndl) { ::FreeLibrary(*hndl); *hndl = nullptr; }
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
