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

#endif // _BF2PY_PYTHON_H_
