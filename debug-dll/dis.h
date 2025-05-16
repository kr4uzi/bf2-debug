#pragma once
#include "python.h"
#include <string>

bool dist_init();
std::string dis(PyCodeObject* co, int lasti = -1);