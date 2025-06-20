#pragma once
#include "python.h"
#include <string>

std::string dis(PyCodeObject* co, int lasti = -1);