#include "usage.h"
#include <print>
#include <filesystem>

void print_usage(const char* procName) {
	std::println("usage: {} -inject=<path-to-bf2py-debug.dll> [-mode={{dry|bf2}}]", std::filesystem::path(procName).filename().string());
}