#include "python.h"
#include <Windows.h>

#include <filesystem>
#if 0
namespace dyn {
	decltype(None) None;
	decltype(Py_NoSiteFlag) Py_NoSiteFlag;

#define BF2_PY_DEF(name) decltype(::name)* name
	BF2_PY_DEF(Py_Initialize);
	BF2_PY_DEF(Py_Finalize);

	BF2_PY_DEF(PyImport_ImportModule);
	BF2_PY_DEF(PyModule_GetDict);
	BF2_PY_DEF(PyDict_GetItemString);
	BF2_PY_DEF(PyObject_CallObject);

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION == 7 && PY_MICRO_VERSION == 18
	BF2_PY_DEF(PyRun_SimpleStringFlags);
	BF2_PY_DEF(Py_IncRef);
	BF2_PY_DEF(Py_DecRef);
#else
	BF2_PY_DEF(PyRun_SimpleString);
#endif
	BF2_PY_DEF(PyErr_Occurred);
	BF2_PY_DEF(PyErr_Print);

	BF2_PY_DEF(PyList_New);
	BF2_PY_DEF(PyTuple_GetItem);
	BF2_PY_DEF(PyString_AsString);
	BF2_PY_DEF(PyString_FromString);
	BF2_PY_DEF(Py_InitModule4);
#undef BF2_PY_LOAD

	bool load_py()
	{
		auto pyDll = LoadLibraryA("dice_py.dll");
		bool success = pyDll != nullptr;
		success = success && ((dyn::None = reinterpret_cast<decltype(dyn::None)>(GetProcAddress(pyDll, "_Py_NoneStruct"))) != nullptr);
		success = success && ((dyn::Py_NoSiteFlag = reinterpret_cast<decltype(dyn::Py_NoSiteFlag)>(GetProcAddress(pyDll, "Py_NoSiteFlag"))) != nullptr);

#define BF2_PY_LOAD(name) (success = success && ((name = reinterpret_cast<decltype(name)>(GetProcAddress(pyDll, #name))) != nullptr))
		BF2_PY_LOAD(Py_Initialize);
		BF2_PY_LOAD(Py_Finalize);

		BF2_PY_LOAD(PyImport_ImportModule);
		BF2_PY_LOAD(PyModule_GetDict);
		BF2_PY_LOAD(PyDict_GetItemString);
		BF2_PY_LOAD(PyObject_CallObject);

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION == 7 && PY_MICRO_VERSION == 18
		BF2_PY_LOAD(PyRun_SimpleStringFlags);
		BF2_PY_LOAD(Py_IncRef);
		BF2_PY_LOAD(Py_DecRef);
#else
		BF2_PY_LOAD(PyRun_SimpleString);
#endif
		BF2_PY_LOAD(PyErr_Occurred);
		BF2_PY_LOAD(PyErr_Print);

		BF2_PY_LOAD(PyList_New);
		BF2_PY_LOAD(PyTuple_GetItem);
		BF2_PY_LOAD(PyString_AsString);
		BF2_PY_LOAD(PyString_FromString);
		BF2_PY_LOAD(Py_InitModule4);
#undef BF2_PY_LOAD

		return success;
	}
}
#endif