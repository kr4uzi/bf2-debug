#include "bf2launch.h"
#include <Windows.h>
#include <detours/detours.h>
#include <print>
#include <format>
#include <iomanip>
#include <sstream>

int run_bf2(const char* procName, const std::vector<std::string>& injectDlls, const std::string& bf2Path, const std::vector<std::string>& bf2args)
{
	STARTUPINFOA si = { .cb = sizeof(si) };
	auto pi = ::PROCESS_INFORMATION{};
	DWORD dwFlags = CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED | DEBUG_PROCESS;
	std::vector<const char*> dlls;
	bool hasDebugDll = false;
	for (const auto& dll : injectDlls) {
		dlls.push_back(dll.c_str());
		if (dll.ends_with("bf2py-debug.dll")) {
			hasDebugDll = true;
		}
	}

	if (!hasDebugDll) {
		dlls.push_back("bf2py-debug.dll");
	}

	std::string commandLine;
	for (const auto& arg : bf2args) {
		std::stringstream ss;
		ss << std::quoted(arg);
		commandLine += std::format(" {}", ss.str());
	}

	auto res = ::DetourCreateProcessWithDllsA("bf2_w32ded.exe", commandLine.data(), nullptr, nullptr, FALSE, dwFlags, nullptr, bf2Path.empty() ? nullptr : bf2Path.c_str(), &si, &pi, dlls.size(), dlls.data(), nullptr);
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