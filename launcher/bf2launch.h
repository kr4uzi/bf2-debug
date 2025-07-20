#pragma once
#include <vector>
#include <filesystem>
#include <string>

int run_bf2_server(const std::filesystem::path& bf2Path, std::wstring bf2args, const std::vector<std::filesystem::path>& injectDlls);
