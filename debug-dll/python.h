#pragma once
#define HAVE_SNPRINTF
//#define MS_NO_COREDLL
//#define Py_ENABLE_SHARED
#ifdef _DEBUG
#define RESTORE_DEBUG
#undef _DEBUG
#endif

#pragma warning(push)
// remove warnings about register members
#pragma warning(disable:5033)
#include <Python.h>
#include <compile.h>
#include <frameobject.h>
#pragma warning(pop)

#ifdef RESTORE_DEBUG
#define _DEBUG
#endif

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION < 7
#  // const-ness was added in python 2.7
#  define BF2PY_CSTR(str) (char*)str
#else
#  define BF2PY_CSTR(str) str
#endif

#ifndef Py_TYPE
#  // first defined in python 2.6
#  define Py_TYPE(ob)             (((PyObject*)(ob))->ob_type)
#endif