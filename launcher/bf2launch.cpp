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

std::wstring to_wstring(const char* str)
{
	auto len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
	auto wstr = std::wstring(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr.data(), len);
	return wstr;
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
				message = "breakpoint => continuing";
			}
			else {
				//continueEvent = DBG_EXCEPTION_NOT_HANDLED;
				message = std::format("Exception caught in PID: {}, Code: {:#x} => continuing", debugEvent.dwProcessId, debugEvent.u.Exception.ExceptionRecord.ExceptionCode);
			}
		}
		else if (event == LOAD_DLL_DEBUG_EVENT) {
			auto& dllInfo = debugEvent.u.LoadDll;
			if (dllInfo.hFile) {
				// Note: FileName is not null-terminated so we need to always allocate one extra null terminator character
				auto fileNameCharSize = sizeof(FILE_NAME_INFO::FileName[0]);
				std::vector<char> fileInfoBuffer(sizeof(FILE_NAME_INFO) + (MAX_PATH + 1) * fileNameCharSize);
				auto fileInfo = reinterpret_cast<FILE_NAME_INFO*>(fileInfoBuffer.data());
				int error = 0;
				if (!GetFileInformationByHandleEx(dllInfo.hFile, FileNameInfo, fileInfo, fileInfoBuffer.size() - fileNameCharSize)) {
					error = GetLastError();
					if (error == ERROR_MORE_DATA) {
						fileInfoBuffer.resize(fileInfoBuffer.size() + fileInfo->FileNameLength);
						error = 0;
						fileInfo = reinterpret_cast<FILE_NAME_INFO*>(fileInfoBuffer.data());
						if (!GetFileInformationByHandleEx(dllInfo.hFile, FileNameInfo, fileInfo, fileInfoBuffer.size() - fileNameCharSize)) {
							error = GetLastError();
						}
					}
				}

				if (error) {
					message = std::format("failed to get file information for dll handle: {}", error);
				}
				else {
					message = std::format("loading dll: {}", to_utf8(fileInfo->FileName));
				}

				// msdn: debugger should close the handle to the DLL handle while processing LOAD_DLL_DEBUG_EVENT
				CloseHandle(dllInfo.hFile);
			}
		}
		else if (event == OUTPUT_DEBUG_STRING_EVENT) {
			const auto& strInfo = debugEvent.u.DebugString;
			std::vector<char> messageBuffer(strInfo.nDebugStringLength); // length of the debug string in bytes (including null terminator)
			SIZE_T bytesRead = 0;
			if (ReadProcessMemory(pi.hProcess, strInfo.lpDebugStringData, messageBuffer.data(), messageBuffer.size(), &bytesRead)) {
				if (strInfo.fUnicode) {
					message = to_utf8(reinterpret_cast<wchar_t*>(messageBuffer.data()));
				}
				else {
					message = messageBuffer.data();
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