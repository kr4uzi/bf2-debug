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
