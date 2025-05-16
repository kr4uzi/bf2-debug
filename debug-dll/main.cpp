#include <Windows.h>
#include <detours/detours.h>
#include "python.h"
#include <type_traits>
#include <print>
#include "debugger.h"
#include "dis.h"
#include <io.h>
#include <fcntl.h>
#include <cstdio>

LONG commitError = 0;
auto bf2_pyInitialize = ::Py_Initialize;
auto bf2_pyInitModule4 = ::Py_InitModule4;
std::unique_ptr<debugger> g_debug = nullptr;
std::thread gth;
#include <chrono>

extern "C" __declspec(dllexport) void pyInitialize()
{
    bf2_pyInitialize();

    if (!debugger::pyInit()) {
        std::println("Failed to initialize debugger");
        return;
    }

    if (!dist_init()) {
		std::println("Failed to initialize dist");
		return;
    }

    // as we only trace a single thread, we only have once trace function and can therefore
    // save the conversion cost of PyCObject_FromVoidPtr and PyCObject_AsVoidPtr
    PyEval_SetTrace([](PyObject* obj, PyFrameObject* frame, int event, PyObject* arg) -> int {
		(void)obj;
        return g_debug->trace_dispatch(frame, event, arg);
    }, nullptr);
}
static_assert(std::is_same_v<decltype(bf2_pyInitialize), decltype(&pyInitialize)>, "bf2 and pydebug Py_Initialize signature must match");

PyCFunction bf2_logWrite = nullptr;
PyObject* log_write(PyObject* self, PyObject* args)
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
		methods[0].ml_meth = log_write;
        return bf2_pyInitModule4(name, methods, doc, self, apiver);
		auto res = bf2_pyInitModule4(name, methods, doc, self, apiver);
        if (res) {
            PyRun_SimpleString(R"(import sys
import host

class fake_stream:
	"""Implements stdout and stderr on top of BF2's log"""
	def __init__(self, name):
		self.name = name
		self.buf = ''

	def write(self, str):
		if len(str) == 0: return
		self.buf += str
		if str[-1] == '\n':
			host.log(self.name + ': ' + self.buf[0:-1])
			self.buf = ''

	def flush(self): pass
	def close(self): pass

sys.stdout = fake_stream('stdout')
sys.stderr = fake_stream('stderr')
print('debugger console connected')
)");
        }
	}

	return bf2_pyInitModule4(name, methods, doc, self, apiver);
}
static_assert(std::is_same_v<decltype(bf2_pyInitModule4), decltype(&pyInitModule4)>, "bf2 and pydebug Py_InitModule4 signature must match");

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved)
{
    (void)hinst;
    (void)reserved;
    
    if (DetourIsHelperProcess()) {
        return TRUE;
    }

    if (dwReason == DLL_PROCESS_ATTACH) {
        g_debug.reset(new debugger());
        g_debug->start();

        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourAttach((PVOID*)&bf2_pyInitialize, pyInitialize);
        DetourAttach((PVOID*)&bf2_pyInitModule4, pyInitModule4);
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
            DetourDetach((PVOID*)&bf2_pyInitModule4, pyInitModule4);
            DetourDetach((PVOID*)&bf2_pyInitialize, pyInitialize);
            commitError = DetourTransactionCommit();
        }
    }

    return TRUE;
}