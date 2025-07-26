#pragma once
#ifndef _BF2PY_PYTHON_H_
#define _BF2PY_PYTHON_H_

#define HAVE_SNPRINTF // prevent python from defining snprintf
#define MS_NO_COREDLL // prevent python2x.lib from being linked
#define Py_ENABLE_SHARED // make Py* symbols be loaded via dice_py.dll
#ifdef _DEBUG // even though this might be a debug build, we need to link to the release version
#  define BF2PY_RESTORE_DEBUG
#  undef _DEBUG
#endif

#pragma warning(push)
// remove warnings about register members
#pragma warning(disable:5033)
#include <Python.h>
#include <compile.h>
#include <frameobject.h>
#include <eval.h>
#pragma warning(pop)

#ifdef BF2PY_RESTORE_DEBUG
#  define _DEBUG
#  undef BF2PY_RESTORE_DEBUG
#endif

#ifndef Py_TYPE
#  // first defined in python 2.6
#  define Py_TYPE(ob)             (((PyObject*)(ob))->ob_type)
#endif

#ifndef PY_RETURN_NONE
#define Py_RETURN_NONE return Py_INCREF(Py_None), Py_None
#endif

#include <memory>
#include <expected>
#include <string>
#include <functional>
namespace bf2py {
	class PyNewRef {
		struct PyDelete {
			void operator()(PyObject* ptr) { Py_DECREF(ptr); }
		};
		std::unique_ptr<PyObject, PyDelete> _ptr;

	public:
		PyNewRef(PyObject* ptr = nullptr) : _ptr(ptr) {}
		PyNewRef& operator=(PyObject* ptr) { _ptr.reset(ptr); return *this; }
		operator bool() { return _ptr.operator bool(); }
		operator PyObject* () { return _ptr.get(); }
	};

	struct py_call_result {
		PyObject* result;
		std::u8string out;
		std::u8string err;
	};

	struct py_utils {
		static std::expected<py_call_result, std::u8string> call(std::function<PyObject* ()> callback);
		static std::string dis(PyCodeObject* co, int lasti = -1);
		static bool init();
	};
}

#endif // _BF2PY_PYTHON_H_
