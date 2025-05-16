#pragma once
#include "python.h"
#include "breakpoint.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>

class bdb
{
    PyFrameObject* botframe;
    PyFrameObject* stopframe;
    PyFrameObject* returnframe;
    std::unordered_map<std::string, std::string> fncache;

public:
    using line_t = Breakpoint::line_t;
    static bool pyInit();
	static std::string normalize_path(const std::string& filename);

protected:
	std::recursive_mutex mutex;
    bool quitting = false;
    Breakpoint* currentbp;
    std::unordered_map<std::string, std::unordered_map<line_t, std::vector<Breakpoint>>> breaks;

protected:
    virtual void user_line(PyFrameObject* frame) = 0;
    virtual void user_call(PyFrameObject* frame) = 0;
    virtual void user_return(PyFrameObject* frame, PyObject* arg) = 0;
    virtual void user_exception(PyFrameObject* frame, PyObject* arg) = 0;
    virtual void do_clear(Breakpoint& bp) = 0;
    virtual void entry(PyFrameObject* frame) = 0;

    std::string canonic(const std::string& filename);
	std::pair<std::deque<std::pair<PyFrameObject*, std::size_t>>, std::size_t> get_stack(PyFrameObject* frame, PyObject* traceback);

public:
    int trace_dispatch(PyFrameObject* frame, int event, PyObject* arg);
    int dispatch_line(PyFrameObject* frame);
    int dispatch_call(PyFrameObject* frame);
    int dispatch_return(PyFrameObject* frame, PyObject* arg);
    int dispatch_exception(PyFrameObject* frame, PyObject* arg);

    bool stop_here(PyFrameObject* frame) const;
    bool break_here(PyFrameObject* frame);
    bool break_anywhere(PyFrameObject* frame);

    void set_step();
    void set_next(PyFrameObject* frame);
    void set_return(PyFrameObject* frame);
    void set_continue();
    void set_quit();
    void set_break(const std::string& filename, line_t line, bool temporary = false, const std::string& cond = "");
};