#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <detours/detours.h>
#include "python.h"
#include <type_traits>
#include <print>
#include "debugger.h"
#include "dis.h"

LONG commitError = 0;
auto bf2_Py_Initialize = ::Py_Initialize;
auto bf2_Py_InitModule4 = ::Py_InitModule4;
auto bf2_Py_Finalize = ::Py_Finalize;
auto bf2_PyErr_PrintEx = ::PyErr_PrintEx;
std::unique_ptr<debugger> g_debug = nullptr;

extern "C" __declspec(dllexport) void pyInitialize()
{
    // before bf2 calls Py_Initialize() it sets Py_NoSiteFlag to 1
    bf2_Py_Initialize();
    // after bf2 calls Py_Initialize() it sets the path variable to ['pylib-2.3.4.zip', 'python', 'mods/bf2/python', 'admin']

    if (!debugger::pyInit()) {
        std::println("Failed to initialize debugger");
        return;
    }

    if (!dist_init()) {
		std::println("Failed to initialize disassembler");
		return;
    }

    g_debug->enable_trace();
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

extern "C" __declspec(dllexport) PyObject* pyInitModule4(char* name, PyMethodDef* methods, char* doc, PyObject* self, int apiver)
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

extern "C" __declspec(dllexport) void pyFinalize()
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
    (void)hinst;
    (void)reserved;
    
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        std::string cmd = GetCommandLineA();
        bool dontStopOnEntry = cmd.contains("+pyDebugStopOnEntry=0");
        g_debug.reset(new debugger(!dontStopOnEntry));
        g_debug->start();

        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourAttach((PVOID*)&bf2_Py_Initialize, pyInitialize);
        DetourAttach((PVOID*)&bf2_Py_InitModule4, pyInitModule4);
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
            DetourDetach((PVOID*)&bf2_Py_InitModule4, pyInitModule4);
            DetourDetach((PVOID*)&bf2_Py_Initialize, pyInitialize);
            commitError = DetourTransactionCommit();
        }
    }

    return TRUE;
}