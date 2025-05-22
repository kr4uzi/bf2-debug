#pragma once
#include "python.h"
#include <string>

bool dist_init();
// cannot use the ootb dis from module dis becaues it prints instead of returning the decompiled string
std::string dis(PyCodeObject* co, int lasti = -1);