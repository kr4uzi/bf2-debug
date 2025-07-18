#include "bf2launch.h"
#include <Windows.h>
#include <detours/detours.h>
#include <print>
#include <format>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <thread>

std::string to_utf8(const wchar_t* wstr)
{
	auto len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
	auto str = std::string(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str.data(), len, nullptr, nullptr);
	return str;
}

int run_bf2(const char* procName, const std::vector<std::string>& injectDlls, const std::string& bf2Path, const std::vector<std::string>& bf2args)
{
	STARTUPINFOW si = { .cb = sizeof(si) };
	PROCESS_INFORMATION pi = {};
	DWORD dwFlags = CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED | DEBUG_PROCESS;

	std::vector<const char*> dlls;
	std::vector<std::string> normalizedDlls;
	bool hasDebugDll = false;
	for (const auto& dll : injectDlls) {
		if (dll.ends_with("bf2py-debug.dll")) {
			hasDebugDll = true;
		}

		auto dllPath = std::filesystem::absolute(dll);
		normalizedDlls.push_back(dllPath.string());
		dlls.push_back(normalizedDlls.back().c_str());
	}

	if (!hasDebugDll) {
		auto dllPath = std::filesystem::absolute("bf2py-debug.dll");
		normalizedDlls.push_back(dllPath.string());
		dlls.push_back(normalizedDlls.back().c_str());
	}

	std::wstring commandLine;
	for (const auto& arg : bf2args) {
		std::wstringstream ss;
		ss << std::quoted(std::wstring{ arg.begin(), arg.end() });
		commandLine += std::format(L" {}", ss.str());
	}

	auto workingDir = bf2Path.empty() ? std::filesystem::current_path() : std::filesystem::path(bf2Path);
	auto exePath = workingDir / "bf2_w32ded.exe";
	bool inheritHandles = false; // the bf2 server creates its own window and must not inerhit any handles
	auto res = ::DetourCreateProcessWithDllsW(exePath.c_str(), commandLine.data(), nullptr, nullptr, inheritHandles, dwFlags, nullptr, workingDir.c_str(), &si, &pi, dlls.size(), dlls.data(), nullptr);
	if (!res) {
		std::println("failed to create process");
		return -1;
	}

	ResumeThread(pi.hThread);

	DEBUG_EVENT debugEvent;
	std::string lastMessage;
	std::size_t repCount = 0;

	// WaitForSingleObject(pi.hProcess, INFINITE);
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
				//continueEvent = DBG_EXCEPTION_NOT_HANDLED;
				message = std::format("Exception caught in PID: {}, Code: {:#x}", debugEvent.dwProcessId, debugEvent.u.Exception.ExceptionRecord.ExceptionCode);
			}
		}
		else if (event == LOAD_DLL_DEBUG_EVENT) {
			auto& dllInfo = debugEvent.u.LoadDll;
			if (dllInfo.hFile) {
				DWORD tmp[1 + 1024 / 2];
				if (GetFileInformationByHandleEx(dllInfo.hFile, FileNameInfo, tmp, sizeof tmp) == 0) {
					std::println("failed to get file information for dll handle");
					break;
				}

				char buf[MAX_PATH * 2];
				FILE_NAME_INFO* info = (FILE_NAME_INFO*)tmp;
				int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, info->FileName, info->FileNameLength / 2, buf, sizeof(buf) - 1, NULL, NULL);
				if (n == 0) {
					std::println("WideCharToMultiByte failed");
					break;
				}
				buf[n] = '\0';
				message = std::format("loading dll: {}", buf);

				CloseHandle(dllInfo.hFile);
			}
		}
		else if (event == OUTPUT_DEBUG_STRING_EVENT) {
			// TODO: this does not yet properly set the message (bytes are reserved, but the length is never set, as such the string is always empty)
			// use a fixed buffer instead and then assign it to message?
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
			// make repeating messages not consume a new line
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

		auto res = ContinueDebugEvent(
			debugEvent.dwProcessId,
			debugEvent.dwThreadId,
			continueEvent
		);
		if (!res) {
			std::println("failed to sent continue event: {}", GetLastError());
			break;
		}
	}	

	DWORD dwResult = -1;
	GetExitCodeProcess(pi.hProcess, &dwResult);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	if (dwResult != 0) {
		std::println("finished with error {}", dwResult);
	}

	return 0;
}