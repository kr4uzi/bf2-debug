#include "unicode.h"
#ifdef _WIN32
#include <Windows.h>
#endif

std::wstring bf2py::from_utf8(const char* str)
{
#ifdef _WIN32
	auto len = ::MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
	auto wstr = std::wstring(len, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr.data(), len);
	wstr.resize(len - 1); // prevenet null terminator from begin part of the string (.size / .length)
	return wstr;
#else
#error "from_utf8 is only implemented for Windows"
#endif
}

std::string bf2py::to_utf8(const wchar_t* wstr)
{
#ifdef _WIN32
	auto len = ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
	auto str = std::string(len, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str.data(), len, nullptr, nullptr);
	str.resize(len - 1); // prevenet null terminator from begin part of the string (.size / .length)
	return str;
#else
#error "to_utf8 is only implemented for Windows"
#endif
}

bool bf2py::argv_to_utf8(int& argc, char**& argv)
{
#ifdef _WIN32
	auto commandLine = GetCommandLineW();
	int wargc = -1;
	auto wargv = CommandLineToArgvW(commandLine, &wargc);
	if (wargc != argc) {
		return false;
	}

	for (int i = 0; i < argc; ++i) {
		auto utf8Str = to_utf8(wargv[i]);
		argv[i] = new char[utf8Str.size() + 1];
		strcpy_s(argv[i], utf8Str.size() + 1, utf8Str.c_str());
	}
#else
	// non-windows use utf-8 by default
#endif
	return true;
}