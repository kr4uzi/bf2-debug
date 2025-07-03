#include "drylaunch.h"
#include "usage.h"
#include "python.h"
#include "host.h"
#include <Windows.h>
#include <print>
#include <iostream>

void py_err_print()
{
	if (PyErr_Occurred()) {
		PyErr_Print();
	}
}

bool init_bf2();
int run_dry(const char* procName, const std::vector<std::string>& injectDlls)
{
	HMODULE debugDll = nullptr;
	for (const auto& path : injectDlls) {
		auto dll = ::LoadLibraryA(path.c_str());
		if (!dll) {
			std::println("Failed to load dll '{}', error: {}", path, GetLastError());
			return 1;
		}

		if (path.ends_with("bf2py-debug.dll")) {
			debugDll = dll;
		}
	}

	if (!debugDll) {
		debugDll = ::LoadLibraryA("bf2py-debug.dll");
	}

	if (!debugDll) {
		std::println("no bf2py-debug.dll found");
	}

	Py_NoSiteFlag = 1;
	Py_Initialize();
	auto finalizer = std::unique_ptr<nullptr_t, void(*)(nullptr_t*)>(
		nullptr, [](nullptr_t*) { py_err_print(); Py_Finalize(); }
	);

	// on lunch, the bf2 executable sets the detected mod path
	if (PyRun_SimpleString("import sys\nsys.path = ['pylib-2.3.4.zip', 'python', 'mods/bf2/python', 'admin']")) {
		py_err_print();
	}

	if (!init_host()) {
		return 1;
	}

	if (!init_bf2()) {
		return 1;
	}

	std::println("python command line (use exit to quit)");
	std::print("> ");
	auto getcl = [](std::string& cmd) {
		Py_BEGIN_ALLOW_THREADS
		std::getline(std::cin, cmd);
		Py_END_ALLOW_THREADS
		return !std::cin.fail();
		};
	for (std::string cmd; getcl(cmd) && cmd != "exit"; std::print("> ")) {
		if (!cmd.empty()) {
			PyRun_SimpleString(cmd.c_str());
			py_err_print();
		}
	}

	return 0;
}

bool init_bf2()
{
	auto bf2Module = PyImport_ImportModule(BF2PY_CSTR("bf2"));
	if (!bf2Module) {
		std::println("Failed to import bf2 module");
		py_err_print();
		return false;
	}

	auto bf2Dict = PyModule_GetDict(bf2Module);
	if (!bf2Dict) {
		std::println("Failed to get bf2 module dict");
		py_err_print();
		return false;
	}

	auto initFunc = PyDict_GetItemString(bf2Dict, "init_module");
	if (!initFunc) {
		std::println("Failed to get init_module function");
		py_err_print();
		return false;
	}

	auto initRes = PyObject_CallObject(initFunc, nullptr);
	if (!initRes) {
		std::println("Failed to call init_module function");
		py_err_print();
		return false;
	}

	Py_DECREF(initRes);
	return true;
}
