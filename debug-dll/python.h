#pragma once
#define HAVE_SNPRINTF
#pragma warning(push)
// remove warnings about register members
#pragma warning(disable:5033)
#include <Python.h>
#include <compile.h>
#include <frameobject.h>
#pragma warning(pop)
#include <memory>
#include <print>