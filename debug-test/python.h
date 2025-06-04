#pragma once
#ifdef _DEBUG
#define BF2PY_RESTORE_DEBUG
#undef _DEBUG
#endif

#pragma warning(push)
// remove warnings about register members
#pragma warning(disable:5033)
#include <Python.h>
#include <pydebug.h>
#pragma warning(pop)

#ifdef BF2PY_RESTORE_DEBUG
#define _DEBUG
#endif

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION < 7
#  // const-ness was added in python 2.7
#  define BF2PY_CSTR(str) (char*)str
#else
#  define BF2PY_CSTR(str) str
#endif

#if 0
#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION == 7 && PY_MICRO_VERSION == 18
#undef Py_DECREF
#undef Py_INCREF
#define Py_DECREF Py_DecRef
#define Py_INCREF Py_IncRef
#endif

namespace dyn {
	extern decltype(Py_NoSiteFlag)* Py_NoSiteFlag;
	extern decltype(Py_None) None;

#define BF2_PY_DECL(name) extern decltype(::name)* name
	BF2_PY_DECL(Py_Initialize);
	BF2_PY_DECL(Py_Finalize);

	BF2_PY_DECL(PyImport_ImportModule);
	BF2_PY_DECL(PyModule_GetDict);
	BF2_PY_DECL(PyDict_GetItemString);
	BF2_PY_DECL(PyObject_CallObject);

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION == 7 && PY_MICRO_VERSION == 18
	BF2_PY_DECL(PyRun_SimpleStringFlags);
	BF2_PY_DECL(Py_DecRef);
	BF2_PY_DECL(Py_IncRef);
#else
	BF2_PY_DECL(PyRun_SimpleString);
#endif
	BF2_PY_DECL(PyErr_Occurred);
	BF2_PY_DECL(PyErr_Print);

	BF2_PY_DECL(PyList_New);
	BF2_PY_DECL(PyTuple_GetItem);
	BF2_PY_DECL(PyString_AsString);
	BF2_PY_DECL(PyString_FromString);
	BF2_PY_DECL(Py_InitModule4);	
#undef BF2_PY_DECL

	bool load_py();
}
#endif