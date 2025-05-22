#include <Windows.h>
#include "python.h"
#include "host.h"
#include <print>
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>
#include <detours/detours.h>
#include <iostream>
#include <sstream>

int run_dry(const char* procName, const std::vector<std::string>& injectDlls);
int run_bf2(const char* procName, const std::vector<std::string>& injectDlls, const std::vector<std::string>& bf2args);
bool init_bf2();
void py_err_print();
std::string bf2_dir_from_registry();

void print_usage(const char* procName) {
	std::println("usage: {} -inject=<path-to-bf2py-debug.dll> [-mode={{dry|bf2}}]", procName);
}

int main(int argc, char* argv[]) {
	std::string mode;
	std::vector<std::string> dlls;
	std::vector<std::string> otherArgs;
	for (int i = 0; i < argc; i++) {
		auto arg = std::string{ argv[i] };
		if (arg.starts_with("-inject=")) {
			dlls.push_back(arg.substr(8));
		}
		else if (arg.starts_with("-mode=")) {
			mode = arg.substr(6);
		}
		else {
			otherArgs.push_back(arg);
		}
	}	

	if (mode.empty()) {
		print_usage(argv[0]);
		std::print("mode: ");
		std::cin >> mode;
	}

	if (mode != "dry" && mode != "bf2") {
		std::println("invalid mode: {}", mode);
		print_usage(argv[0]);
		return 1;
	}

	if (mode == "dry") {
		run_dry(argv[0], dlls);
	}
	else if (mode == "bf2") {
		run_bf2(argv[0], dlls, otherArgs);
	}

	return 0;
}

int run_bf2(const char* procName, const std::vector<std::string>& injectDlls, const std::vector<std::string>& bf2args)
{
	STARTUPINFOA si = { .cb = sizeof(si) };
	auto pi = ::PROCESS_INFORMATION{};
	DWORD dwFlags = CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED | DEBUG_PROCESS;
	std::vector<const char*> dlls;
	for (const auto& dll : injectDlls)
		dlls.push_back(dll.c_str());
	
	std::string commandLine;
	for (const auto& arg : bf2args) {
		std::stringstream ss;
		ss << std::quoted(arg);
		commandLine += std::format(" {}", ss.str());
	}

	auto res = ::DetourCreateProcessWithDllsA("bf2_w32ded.exe", commandLine.data(), nullptr, nullptr, FALSE, dwFlags, nullptr, nullptr, &si, &pi, dlls.size(), dlls.data(), nullptr);
	if (!res) {
		std::println("failed to create process");
		return -1;
	}

	ResumeThread(pi.hThread);

	DEBUG_EVENT debugEvent;
	std::string lastMessage;
	std::size_t repCount = 0;
	while (WaitForDebugEvent(&debugEvent, INFINITE)) {
		DWORD continueEvent = DBG_CONTINUE;
		auto event = debugEvent.dwDebugEventCode;
		std::string message;
		if (event == EXCEPTION_DEBUG_EVENT) {
			auto code = debugEvent.u.Exception.ExceptionRecord.ExceptionCode;
			
			if (code == STATUS_BREAKPOINT) {
				message = "breakpoint";
			}
			else {
				continueEvent = DBG_EXCEPTION_NOT_HANDLED;
				message = std::format("Exception caught in PID: {}, Code: {:#x}", debugEvent.dwProcessId, debugEvent.u.Exception.ExceptionRecord.ExceptionCode);
			}
		}
		else if (event == OUTPUT_DEBUG_STRING_EVENT) {
			auto& strInfo = debugEvent.u.DebugString;
			auto numBytes = strInfo.nDebugStringLength * (strInfo.fUnicode ? sizeof(wchar_t) : sizeof(char));
			message.reserve(numBytes);
			SIZE_T bytesRead = 0;
			if (ReadProcessMemory(pi.hProcess, strInfo.lpDebugStringData, message.data(), numBytes, &bytesRead)) {
				if (strInfo.fUnicode) {
					WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(message.data()), strInfo.nDebugStringLength, message.data(), message.capacity(), nullptr, nullptr);
				}
			}
			else {
				message = std::format("Failed to read Debug Message (ReadProcessMemory failed with error {})", GetLastError());
			}
		}
		else if (event == EXIT_PROCESS_DEBUG_EVENT) {
			break;
		}

		if (!message.empty()) {
			if (lastMessage.empty()) {
				lastMessage = message;
				std::print("{}\r", message);
			}
			else if (message == lastMessage) {
				lastMessage = message;
				std::print("{} ({}x)\r", message, 1 + (++repCount));
			}
			else {
				repCount = 0;
				lastMessage = message;
				std::print("\n{}\r", message);
			}
		}

		ContinueDebugEvent(
			debugEvent.dwProcessId,
			debugEvent.dwThreadId,
			continueEvent
		);
	}
	// WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD dwResult = 500;
	GetExitCodeProcess(pi.hProcess, &dwResult);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	if (dwResult != 0) {
		std::println("finished with error {}", dwResult);
	}

	return 0;
}

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
		std::println("bf2py-debug.dll must be injected");
		print_usage(procName);
		return 1;
	}

	auto pyInitialize = GetProcAddress(debugDll, "pyInitialize");
	if (!pyInitialize) {
		std::println("Failed to get Py_Initialize address: {}", GetLastError());
		return 1;
	}

	auto pyInitModule4 = GetProcAddress(debugDll, "pyInitModule4");
	if (!pyInitModule4) {
		std::println("Failed to get Py_InitModule4 address: {}", GetLastError());
		return 1;
	}

	auto pyFinalize = GetProcAddress(debugDll, "pyFinalize");
	if (!pyFinalize) {
		std::println("Failed to get Py_Finalize address: {}", GetLastError());
		return 1;
	}

	// Battlefield 2 initalizes python with Py_NoSiteFlag = 1
	Py_NoSiteFlag = 1;
	reinterpret_cast<void(*)()>(pyInitialize)();
	auto finalizer = std::unique_ptr<FARPROC, void(*)(FARPROC*)>(
		&pyFinalize, [](FARPROC* fn) { py_err_print(); reinterpret_cast<void(*)()>(*fn)(); }
	);

	if (PyRun_SimpleString("import sys\nsys.path = ['pylib-2.3.4.zip', 'python', 'mods/bf2/python', 'admin']")) {
		py_err_print();
	}

	if (!init_host(pyInitModule4)) {
		return 1;
	}

	if (!init_bf2()) {
		return 1;
	}
	
	std::println("python command line (use exit to quit)");
	for (std::string cmd; std::getline(std::cin, cmd) && cmd != "exit"; std::print("> ")) {
		if (!cmd.empty()) {
			PyRun_SimpleString(cmd.c_str());
			py_err_print();
		}
	}

	return 0;
}

bool init_bf2()
{
	char moduleName[] = "bf2";
	auto bf2Module = PyImport_ImportModule(moduleName);
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

	Py_DECREF(initRes);
	return true;
}

void py_err_print()
{
	if (PyErr_Occurred()) {
		PyErr_Print();
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