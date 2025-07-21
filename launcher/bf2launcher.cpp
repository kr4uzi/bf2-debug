#include "bf2launcher.h"
using namespace bf2py;

#ifdef _WIN32
#include <Windows.h>
#include <detours/detours.h>
#include <iostream>
#include <expected>
#include <print>
#include <map>
#include "unicode.h"

namespace {
	std::wstring error_message_from_error_code(DWORD errorCode)
	{
		auto messageFlags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER;
		auto langId = 0; // MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
		wchar_t* messageBuffer = nullptr;
		auto msgLength = FormatMessageW(messageFlags, nullptr, errorCode, langId, reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
		if (msgLength > 0) {
			auto bufferCleanup = std::unique_ptr<wchar_t, void(*)(wchar_t*)>{
				messageBuffer,
				[](auto buff) { ::LocalFree(buff); }
			};

			return messageBuffer;
		}

		return std::format(L"Failed to retrieve error message for code {}", errorCode);
	}

	std::expected<std::wstring, DWORD> fileNameFromHandle(HANDLE file)
	{
		// Note: FileName is not null-terminated so we need to always allocate one extra null terminator character
		auto fileNameCharSize = sizeof(FILE_NAME_INFO::FileName[0]);
		auto fileInfoBuffer = std::vector<char>(sizeof(FILE_NAME_INFO) + (MAX_PATH + 1) * fileNameCharSize, '\0');
		auto fileInfo = reinterpret_cast<FILE_NAME_INFO*>(fileInfoBuffer.data());
		auto error = DWORD(0);

		// only let GetFileInformationByHandleEx use n-1 bytes so that the last character remains zero'ed
		if (!GetFileInformationByHandleEx(file, FileNameInfo, fileInfo, fileInfoBuffer.size() - fileNameCharSize)) {
			error = GetLastError();
			if (error == ERROR_MORE_DATA) {
				fileInfoBuffer.resize(fileInfoBuffer.size() + fileInfo->FileNameLength);
				error = 0;
				fileInfo = reinterpret_cast<FILE_NAME_INFO*>(fileInfoBuffer.data());

				if (!GetFileInformationByHandleEx(file, FileNameInfo, fileInfo, fileInfoBuffer.size() - fileNameCharSize)) {
					error = GetLastError();
				}
			}
		}

		if (error) {
			return std::unexpected(error);
		}

		return fileInfo->FileName;
	}
}
#endif

bool launcher::checkAndNormalizeDLLs()
{
#ifdef _WIN32
	if (_dlls.size() == _detourDllPtrs.size()) {
		// already normalized
		return true;
	}

	for (const auto& dll : _dlls) {
		try {
			auto absolutePath = std::filesystem::absolute(dll);
			_normalizedDlls.push_back(absolutePath.string());
		}
		catch (std::system_error& e) {
			auto utf8Path = dll.u8string();
			// while the printable path might not look pretty, it will at least not crash the console
			auto printablePath = std::string{ utf8Path.begin(), utf8Path.end() };
			std::println("error preparing dll: {}\ncaused by dll: {}", e.what(), printablePath);
			return false;
		}

		_detourDllPtrs.push_back(_normalizedDlls.back().c_str());
	}

	return true;
#else
	return true;
#endif
}

int launcher::run()
{
	if (!checkAndNormalizeDLLs()) {
		return -1;
	}

#ifdef _WIN32
	STARTUPINFOW si = { .cb = sizeof(si) };
	PROCESS_INFORMATION pi = {};
	DWORD dwFlags = CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED | DEBUG_PROCESS;

	std::wcout << "exec: " << _exe
		<< "\nargs: " << (_args.empty() ? L"<none>" : _args)
		<< "\ndir : " << _path
		<< "\ndlls: ";

	if (_detourDllPtrs.empty()) {
		std::wcout << L"<none>" << std::endl;
	}
	else {
		std::wcout << std::endl;
		for (const auto& dll : _detourDllPtrs) {
			std::println(" {}", dll);
		}
	}
	
	auto exePath = _path / _exe;
	bool inheritHandles = false; // the bf2 server creates its own window and must not inerhit any handles
	auto res = ::DetourCreateProcessWithDllsW(exePath.c_str(), _args.data(), nullptr, nullptr, inheritHandles, dwFlags, nullptr, _path.c_str(), &si, &pi, _detourDllPtrs.size(), _detourDllPtrs.data(), nullptr);
	if (!res) {
		std::println("failed to create process: {}", ::GetLastError());
		return -1;
	}

	ResumeThread(pi.hThread);

	DEBUG_EVENT debugEvent;
	std::wstring lastMessage;
	std::size_t repCount = 0;
	// WaitForSingleObject(pi.hProcess, INFINITE);
	while (WaitForDebugEvent(&debugEvent, INFINITE)) {
		DWORD continueEvent = DBG_CONTINUE;
		auto event = debugEvent.dwDebugEventCode;
		std::wstring message;
		if (event == EXCEPTION_DEBUG_EVENT) {
			auto code = debugEvent.u.Exception.ExceptionRecord.ExceptionCode;

			if (code == STATUS_BREAKPOINT) {
				message = L"breakpoint => continuing";
			}
			else {
				//continueEvent = DBG_EXCEPTION_NOT_HANDLED;
				message = std::format(L"Exception caught in PID: {}, Code: {:#x} => continuing", debugEvent.dwProcessId, debugEvent.u.Exception.ExceptionRecord.ExceptionCode);
			}
		}
		else if (event == LOAD_DLL_DEBUG_EVENT) {
			auto& dllInfo = debugEvent.u.LoadDll;
			if (dllInfo.hFile) {
				// msdn: debugger should close the handle to the DLL handle while processing LOAD_DLL_DEBUG_EVENT
				auto fileCloser = std::unique_ptr<void, void(*)(HANDLE)>{ dllInfo.hFile, [](HANDLE h) { CloseHandle(h); } };
				auto fileName = ::fileNameFromHandle(dllInfo.hFile);
				if (fileName) {
					message = std::format(L"loading dll: {}", *fileName);
				}
				else {
					auto error = fileName.error();
					message = std::format(L"failed to get file information for dll handle: {}", error_message_from_error_code(error));
				}
			}
		}
		else if (event == OUTPUT_DEBUG_STRING_EVENT) {
			const auto& strInfo = debugEvent.u.DebugString;
			std::vector<char> messageBuffer(strInfo.nDebugStringLength); // length of the debug string in bytes (including null terminator)
			SIZE_T bytesRead = 0;
			if (::ReadProcessMemory(pi.hProcess, strInfo.lpDebugStringData, messageBuffer.data(), messageBuffer.size(), &bytesRead)) {
				if (strInfo.fUnicode) {
					message = reinterpret_cast<wchar_t*>(messageBuffer.data());
				}
				else {
					message = bf2py::from_utf8(messageBuffer.data());
				}
			}
			else {
				auto error = ::GetLastError();
				message = std::format(L"Failed to read Debug Message (ReadProcessMemory failed with error {})", GetLastError());
			}
		}
		else if (event == EXIT_PROCESS_DEBUG_EVENT) {
			break;
		}

		if (!message.empty()) {
			// make repeating messages not consume a new line
			if (lastMessage.empty()) {
				lastMessage = message;
				std::wcout << message << '\r';
			}
			else if (message == lastMessage) {
				lastMessage = message;
				std::wcout << message << " (" << (1 + (++repCount)) << "x)\r";
			}
			else {
				repCount = 0;
				lastMessage = message;
				std::wcout << '\n' << message << '\r';
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
#else
#error "run_bf2_server is only implemented for Windows"
#endif
}

std::string launcher::getPythonVersion() const
{
#ifdef _WIN32
	if (!::SetDllDirectoryW(_path.c_str())) {
		auto errorCode = ::GetLastError();
		std::wcerr << "Failed to set DLL directory to " << _path << ": " << ::error_message_from_error_code(errorCode) << std::endl;
		return {};
	}

	auto restoreDllDirectory = std::unique_ptr<void, void(*)(void*)>(
		nullptr, [](void*) { ::SetDllDirectoryW(nullptr); }
	);

	auto dicePyPath = _path / L"dice_py.dll";
	auto handle = ::LoadLibraryW(dicePyPath.c_str());
	if (!handle) {
		auto errorCode = ::GetLastError();
		std::wcerr << "Failed to load Python DLL from " << dicePyPath << ": " << ::error_message_from_error_code(errorCode) << std::endl;
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

bool launcher::setPathFromRegistry()
{
#ifdef _WIN32
	std::map<std::wstring, std::wstring> regPaths = {
		{ LR"(SOFTWARE\EA GAMES\Battlefield 2 Server)", L"GAMEDIR" },
		{ LR"(SOFTWARE\Electronic Arts\EA Games\Battlefield 2)", L"InstallDir"},
	};

	for (const auto& [path, key] : regPaths) {
		DWORD size = 0;
		auto res = RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), key.c_str(), RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, nullptr, &size);
		if (res != ERROR_SUCCESS) {
			continue;
		}

		std::vector<char> buffer(size);
		res = RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), key.c_str(), RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buffer.data(), &size);
		if (res != ERROR_SUCCESS) {
			auto errorCode = ::GetLastError();
			std::wcerr << "RegGetValueW failed (" << errorCode << ": " << ::error_message_from_error_code(errorCode) << std::endl;
			continue;
		}

		// REG_SZ is zero-terminated
		_path = reinterpret_cast<wchar_t*>(buffer.data());
		return true;
	}
#endif

	return false;
}