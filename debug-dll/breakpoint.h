#pragma once
#include <string>
#include <cstdint>

struct Breakpoint
{
    typedef std::int64_t line_t;

    const std::string filename;
    const line_t line;
    const bool temporary;
    const std::string condition;

    std::string command;
    bool enabled = true;

    std::size_t ignore = 0;
    std::size_t hits = 0;

    Breakpoint(const std::string& filename, line_t line, bool temporary = false, const std::string& condition = "")
        : filename(filename), line(line), temporary(temporary), condition(condition)
    {

    }
};