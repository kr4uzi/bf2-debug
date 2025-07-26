#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <detours/detours.h>
#include <type_traits>
#include <print>
#include <functional>
#include <map>
#include "debugger.h"
#include "output_redirect.h"

LONG commitError = 0;
auto bf2_AllocConsole = ::AllocConsole;
auto bf2_Py_Initialize = ::Py_Initialize;
auto bf2_Py_InitModule4 = ::Py_InitModule4;
auto bf2_PyEval_InitThreads = ::PyEval_InitThreads;
auto bf2_Py_Finalize = ::Py_Finalize;
bool forwardOutput = false;
bf2py::debugger g_debug;
bf2py::output_redirect g_stdout_redirect, g_stderr_redirect;

void output_callback(std::u8string msg) {
    msg += u8'\n';

#ifdef _WIN32
    auto str = reinterpret_cast<const char*>(msg.data());
    auto len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    auto wstr = std::wstring(len, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr.data(), len);
    ::OutputDebugStringW(wstr.c_str());
#else
    ::syslog(LOG_ERR, "%s: %s", message.c_str(), ::strerror(errno));
#endif

    g_debug.log(msg);
    if (forwardOutput) {
        ::_write(g_stdout_redirect.output_fd(), msg.data(), msg.size());
    }
}

BOOL __stdcall allocConsole()
{
    // note: anything printed (print/cout/printf) until this point is not visible
	auto res = bf2_AllocConsole();
    
    // stdout needs to be reinitialized after the console is allocated
    FILE* f;
    ::freopen_s(&f, "CONOUT$", "w", stdout);
    ::freopen_s(&f, "CONOUT$", "w", stderr);
    g_stderr_redirect.callback(output_callback);
    g_stdout_redirect.callback(output_callback);

    g_stdout_redirect.start(stdout);
    g_stderr_redirect.start(stderr);

	return res;
}
static_assert(std::is_same_v<decltype(bf2_AllocConsole), decltype(&allocConsole)>, "bf2 and pydebug AllocConsole signature must match");

std::string error_message_from_error_code(DWORD errorCode)
{
    auto messageFlags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER;
    auto langId = 0; // MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
    char* messageBuffer = nullptr;
    auto msgLength = FormatMessageA(messageFlags, nullptr, errorCode, langId, reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr);
    if (msgLength > 0) {
        auto bufferCleanup = std::unique_ptr<char, void(*)(char*)>{
            messageBuffer,
            [](auto buff) { ::LocalFree(buff); }
        };

        return messageBuffer;
    }

    return std::format("Failed to retrieve error message for code {}", errorCode);
}

namespace {
    struct redirector : PyObject {
        std::function<void(const char*)> callback;

        redirector() = default;
        ~redirector() = default;
    };

    PyObject* redirector_write(PyObject* _self, PyObject* text) {
        auto self = static_cast<redirector*>(_self);
        self->callback(PyString_AS_STRING(text));
        Py_RETURN_NONE;
    }

    PyObject* redirector_flush(PyObject* _self, PyObject* text) {
        Py_RETURN_NONE;
    }

    PyMethodDef redirector_methods[] = {
        { (char*)"write", redirector_write, METH_O, nullptr },
        { (char*)"flush", redirector_flush, METH_NOARGS, nullptr },
        { }
    };

    PyTypeObject redirector_type = {
        .ob_refcnt = 1,
        .tp_name = (char*)"bf2py.redirector",
        .tp_basicsize = sizeof(redirector),
        .tp_dealloc = [](PyObject* _self) {
            auto self = static_cast<redirector*>(_self);
            self->~redirector();
            redirector_type.tp_free(self);
        },
        .tp_flags = Py_TPFLAGS_DEFAULT,
        .tp_methods = redirector_methods
    };
}

void pyInitialize()
{
    // note: before bf2 calls Py_Initialize() it sets Py_NoSiteFlag to 1
    bf2_Py_Initialize();

    // using a custom printer, because:
    // for some reason, when reinitializing the stdout/stderr using PySys_SetObject and PyFile_FromFile,
    // all prints resulted in an "[Error 9] Bad file descriptor" (even though the pointers were correct...)
    if (PyType_Ready(&::redirector_type) == 0) {
        for (auto&& [name, stream] : std::map<const char*, std::FILE*>{ { "stdout", stdout }, { "stderr", stderr } }) {
            auto redirect = PyObject_NEW(::redirector, &::redirector_type);
            if (!redirect) {
                std::println(stderr, "failed to initialize sys.{}", name);
                continue;
            }

            new (redirect) redirector();

            redirect->ob_refcnt = 1;
            redirect->ob_type = &redirector_type;
            redirect->ob_type->ob_refcnt++;
            redirect->callback = [stream](const char* str) {
                std::print(stream, "{}", str);
            };

            if (PySys_SetObject(const_cast<char*>(name), redirect) != 0) {
                std::println(stderr, "failed to set sys.{}", name);
                Py_DECREF(redirect);
            }
        }
    }
    else {
        std::println(stderr, "unable to set python stdout/stderr redirects");
    }

    if (!bf2py::py_utils::init()) {
        std::println("failed to initialize py_utils");
        return;
    }

    if (!bf2py::debugger::pyInit()) {
        std::println("Failed to initialize debugger");
        return;
    }

    g_debug.enable_trace();

    // after bf2 calls Py_Initialize() it sets the path variable to ['pylib-2.3.4.zip', 'python', 'mods/bf2/python', 'admin']
	// any initializeation done here which depends on python modules need to do their own path initialization
    // Note: bf2 sets the path *not* using PySys_SetObject, but with PyRun_SimpleString
}
static_assert(std::is_same_v<decltype(bf2_Py_Initialize), decltype(&pyInitialize)>, "bf2 and pydebug Py_Initialize signature must match");

PyCFunction bf2_logWrite = nullptr;
PyObject* logWrite(PyObject* self, PyObject* args)
{
    auto item = PyTuple_GetItem(args, 0);
    auto str = PyString_AsString(item);

	g_debug.log(std::format("[bf2] {}\n", str));

    if (bf2_logWrite) {
		return bf2_logWrite(self, args);
    }

    Py_RETURN_NONE;
}

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION < 7
PyObject* pyInitModule4(char* name, PyMethodDef* methods, char* doc, PyObject* self, int apiver)
#else
PyObject* pyInitModule4(const char* name, PyMethodDef* methods, const char* doc, PyObject* self, int apiver)
#endif
{
	if (strcmp(name, "host") == 0) {
        bf2_logWrite = methods[0].ml_meth;
		methods[0].ml_meth = logWrite;

        std::map<std::string, PyCFunction> fns;
        for (auto i = methods; i != nullptr && i->ml_name != nullptr; i++) {
            fns.emplace(i->ml_name, i->ml_meth);
        }

        g_debug.setHostModule(fns);
	}

	return bf2_Py_InitModule4(name, methods, doc, self, apiver);
}
static_assert(std::is_same_v<decltype(bf2_Py_InitModule4), decltype(&pyInitModule4)>, "bf2 and pydebug Py_InitModule4 signature must match");

void pyEval_InitThreads()
{
    bf2_PyEval_InitThreads();
    g_debug.enable_thread_trace();
}
static_assert(std::is_same_v<decltype(bf2_PyEval_InitThreads), decltype(&pyEval_InitThreads)>, "bf2 and pydebug PyEval_InitThreads signature must match");

void pyFinalize()
{
    g_debug.stop();
    g_debug.disable_trace();

    bf2_Py_Finalize();
}
static_assert(std::is_same_v<decltype(bf2_Py_Finalize), decltype(&pyFinalize)>, "bf2 and pydebug Py_Finalize signature must match");

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
	// Note: Any outputs will not be visible (if launched via bf2) until AllocConsole is called
    (void)hinst;
    (void)reserved;
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
		// by default, we stop on entry, unless the command line specifies otherwise
        std::wstring cmd = GetCommandLineW();
        if (cmd.contains(L"+pyDebugStopOnEntry=0")) {
            g_debug.wait_for_connection(false);
        }

        if (cmd.contains(L"+pyDebugForwardOutput=1")) {
            forwardOutput = true;
        }

        g_debug.start();      

        DetourRestoreAfterWith();
        DetourTransactionBegin();
        // enable output to debugger console redirection
        DetourAttach((PVOID*)&bf2_AllocConsole, allocConsole);
		// initialize the debugger when bf2 calls Py_Initialize
        DetourAttach((PVOID*)&bf2_Py_Initialize, pyInitialize);
        // when bf2 initializes the host module, we modify the log function (redirect output to the debugger)
        DetourAttach((PVOID*)&bf2_Py_InitModule4, pyInitModule4);
        // when threads are enabled, we start thread tracing
		DetourAttach((PVOID*)&bf2_PyEval_InitThreads, pyEval_InitThreads);
		// shutdown the debugger when bf2 calls Py_Finalize
        DetourAttach((PVOID*)&bf2_Py_Finalize, pyFinalize);
        DetourUpdateThread(GetCurrentThread());
        commitError = DetourTransactionCommit();
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        if (!commitError) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach((PVOID*)&bf2_Py_Finalize, pyFinalize);
			DetourDetach((PVOID*)&bf2_PyEval_InitThreads, pyEval_InitThreads);
            DetourDetach((PVOID*)&bf2_Py_InitModule4, pyInitModule4);
            DetourDetach((PVOID*)&bf2_Py_Initialize, pyInitialize);
            DetourDetach((PVOID*)&bf2_AllocConsole, allocConsole);
            commitError = DetourTransactionCommit();
            g_debug.~debugger();
        }
    }

    if (commitError) {
        std::println("failed to detour Py_Initialize: {}", commitError);
    }

    return TRUE;
}
