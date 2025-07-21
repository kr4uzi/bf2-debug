#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <detours/detours.h>
#include "python.h"
#include <type_traits>
#include <print>
#include "debugger.h"
#include "dis.h"

LONG commitError = 0;
auto bf2_AllocConsole = ::AllocConsole;
auto bf2_Py_Initialize = ::Py_Initialize;
auto bf2_Py_InitModule4 = ::Py_InitModule4;
auto bf2_PyEval_InitThreads = ::PyEval_InitThreads;
auto bf2_Py_Finalize = ::Py_Finalize;
std::unique_ptr<bf2py::debugger> g_debug = nullptr;

BOOL __stdcall allocConsole()
{
	auto res = bf2_AllocConsole();

	// stdout needs to be reinitialized after the console is allocated
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);

    if (g_debug) {
        g_debug->start_redirect_output();
    }

	return res;
}
static_assert(std::is_same_v<decltype(bf2_AllocConsole), decltype(&allocConsole)>, "bf2 and pydebug AllocConsole signature must match");

void pyInitialize()
{
    // note: before bf2 calls Py_Initialize() it sets Py_NoSiteFlag to 1
    bf2_Py_Initialize();

    if (!bf2py::debugger::pyInit()) {
        std::println("Failed to initialize debugger");
        return;
    }

    g_debug->enable_trace();

    // after bf2 calls Py_Initialize() it sets the path variable to ['pylib-2.3.4.zip', 'python', 'mods/bf2/python', 'admin']
	// any initializeation done here which depends on python modules need to do their own path initialization
}
static_assert(std::is_same_v<decltype(bf2_Py_Initialize), decltype(&pyInitialize)>, "bf2 and pydebug Py_Initialize signature must match");

PyCFunction bf2_logWrite = nullptr;
PyObject* logWrite(PyObject* self, PyObject* args)
{
    auto item = PyTuple_GetItem(args, 0);
    auto str = PyString_AsString(item);

	if (g_debug) {
		g_debug->log(std::format("[bf2] {}\n", str));
	}

    if (bf2_logWrite) {
		return bf2_logWrite(self, args);
    }

    Py_INCREF(Py_None);
    return Py_None;
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

        if (g_debug) {
            std::map<std::string, PyCFunction> fns;
            for (auto i = methods; i != nullptr && i->ml_name != nullptr; i++) {
                fns.emplace(i->ml_name, i->ml_meth);
            }

            g_debug->setHostModule(fns);
        }
	}

	return bf2_Py_InitModule4(name, methods, doc, self, apiver);
}
static_assert(std::is_same_v<decltype(bf2_Py_InitModule4), decltype(&pyInitModule4)>, "bf2 and pydebug Py_InitModule4 signature must match");

void pyEval_InitThreads()
{
    bf2_PyEval_InitThreads();
    if (g_debug) {
        g_debug->enable_thread_trace();
    }
}
static_assert(std::is_same_v<decltype(bf2_PyEval_InitThreads), decltype(&pyEval_InitThreads)>, "bf2 and pydebug PyEval_InitThreads signature must match");

void pyFinalize()
{
    if (g_debug) {
        g_debug->stop();
        g_debug->disable_trace();
    }

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
        std::string cmd = GetCommandLineA();
        bool dontStopOnEntry = cmd.contains("+pyDebugStopOnEntry=0");
        g_debug.reset(new bf2py::debugger(!dontStopOnEntry));
        g_debug->start();      

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
        if (commitError) {
			std::println("failed to detour Py_Initialize: {}", commitError);
        }
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
        }
    }

    return TRUE;
}
