#pragma once
#ifndef _LAUNCHER_UNICODE_H_
#define _LAUNCHER_UNICODE_H_
#include <string>

namespace bf2py {
	std::wstring from_utf8(const char* str);
	std::string to_utf8(const wchar_t* wstr);
	bool argv_to_utf8(int& argc, char**& argv);
}

#endif
