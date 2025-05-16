#include <Windows.h>
#include "python.h"
#include "host.h"
#include <print>
#include <filesystem>
#include <map>

bool init_bf2();
void py_err_print();
std::string bf2_dir_from_registry();

int main(int argc, char* argv[]) {
	if (argc < 2) {
		std::println("Usage: {} <bf2py-debug.dll path>", argv[0]);
		return 1;
	}

	auto dll = LoadLibraryA(argv[1]);
	if (!dll) {
		std::println("Failed to load dll: {}", GetLastError());
		return 1;
	}	

	auto pyInitialize = GetProcAddress(dll, "pyInitialize");
	if (!pyInitialize) {
		std::println("Failed to get function address: {}", GetLastError());
		return 1;
	}

	auto pyInitModule4 = GetProcAddress(dll, "pyInitModule4");
	if (!pyInitModule4) {
		std::println("Failed to get function address: {}", GetLastError());
		return 1;
	}

	// Battlefield 2 initalizes python with Py_NoSiteFlag = 1
	Py_NoSiteFlag = 1;
	reinterpret_cast<void(*)()>(pyInitialize)();

	if (init_host(pyInitModule4)) {
		if (init_bf2()) {
			std::println("bf2 initialized");
		} else {
			py_err_print();
		}
	}
	else {
		py_err_print();
	}

	return 0;
}

bool init_bf2()
{
	auto initStr = std::format("import sys\nsys.path = ['pylib-2.3.4.zip', 'python', 'mods/bf2/python', 'admin']");
	PyRun_SimpleString(initStr.c_str());
	auto bf2Module = PyImport_ImportModule((char*)"bf2");
	if (!bf2Module) {
		std::println("Failed to import bf2 module");
		return false;
	}

	auto bf2Dict = PyModule_GetDict(bf2Module);
	if (!bf2Dict) {
		std::println("Failed to get bf2 module dict");
		return false;
	}

	auto initFunc = PyDict_GetItemString(bf2Dict, "init_module");
	if (!initFunc) {
		std::println("Failed to get init_module function");
		return false;
	}

	auto initRes = PyObject_CallObject(initFunc, nullptr);
	if (!initRes) {
		std::println("Failed to call init_module function");
		return false;
	}

	return true;
}

void py_err_print()
{
	if (PyErr_Occurred()) {
		PyObject* ptype, *pvalue, *ptraceback;
		PyErr_Fetch(&ptype, &pvalue, &ptraceback);
		PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);

		PyObject* ptype_str = PyObject_Str(ptype);
		PyObject* pvalue_str = PyObject_Str(pvalue);

		if (ptype_str && pvalue_str) {
			std::println("Exception occurred:\n{}\n{}", PyString_AsString(ptype_str), PyString_AsString(pvalue_str));
		}

		Py_XDECREF(pvalue_str);
		Py_XDECREF(ptype_str);
		Py_XDECREF(ptraceback);
		Py_XDECREF(pvalue);
		Py_XDECREF(ptype);
	}
}

std::string bf2_dir_from_registry()
{
	std::map<std::string, std::string> regPaths = {
		{ R"(SOFTWARE\EA GAMES\Battlefield 2 Server)", "GAMEDIR" },
		{ R"(SOFTWARE\Electronic Arts\EA Games\Battlefield 2)", "InstallDir"},
	};

	for (const auto& [path, key] : regPaths) {
		HKEY hKey = nullptr;
		auto res = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_WOW64_32KEY | KEY_READ, &hKey);
		if (res != ERROR_SUCCESS) {
			continue;
		}

		if (!hKey) {
			std::println("Failed to open {}", path);
			continue;
		}

		BYTE value[MAX_PATH];
		DWORD size = sizeof(value);
		DWORD type = REG_SZ;
		res = RegQueryValueExA(hKey, key.c_str(), nullptr, &type, value, &size);
		if (res != ERROR_SUCCESS) {
			std::println("Failed to open {}:{}", path, key);
			continue;
		}

		// note: value might not be null terminated
		return std::string{ value, value + size };
	}

	return "";
}