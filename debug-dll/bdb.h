#pragma once
#include "breakpoint.h"
#include "python.h"
#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class bdb
{
    std::set<PyFrameObject*> _ignored_frames;
    bool _evaling = false;
    bool _step = false;
    PyFrameObject* _stopframe = nullptr;
    PyFrameObject* _returnframe = nullptr;
    std::unordered_map<std::string, std::string> _fncache;

public:
    using line_t = Breakpoint::line_t;
	static std::string normalize_path(const std::string& filename);

    enum class exception_mode : unsigned char {
        NEVER = 0,
        UNHANDLED_EXCEPTION = 1,
        HANDLED_EXCEPTION = 2,
        ALL_EXCEPTIONS = 3
    };

    static std::pair<std::deque<std::pair<PyFrameObject*, std::size_t>>, std::size_t> get_stack(PyFrameObject* frame, PyObject* traceback);

protected:
    bool _quitting = false;
    Breakpoint* _currentbp = nullptr;
    std::unordered_map<std::string, std::unordered_map<line_t, std::vector<Breakpoint>>> _breaks;
    exception_mode _exmode = exception_mode::NEVER;

protected:
	virtual void user_entry(PyFrameObject* frame) = 0;
    virtual void user_call(PyFrameObject* frame) = 0;
    virtual void user_line(PyFrameObject* frame) = 0;
    virtual void user_return(PyFrameObject* frame, PyObject* arg) = 0;
    virtual void user_exception(PyFrameObject* frame, PyObject* arg) = 0;
    virtual void do_clear(Breakpoint& bp) = 0;
    virtual void on_breakpoint_error(Breakpoint& bp, const std::string& msg) = 0;

    void reset();
    void raiseException(const std::string& message);

public:
    bdb();
    ~bdb();

    static bool pyInit();

    void trace_thread(PyThreadState* tstate);
    void disable_trace();
    bool trace_ignore() const { return _evaling || _quitting; }

    std::string canonic(const std::string& filename);

    virtual int trace_dispatch(PyFrameObject* frame, int event, PyObject* arg);
    virtual int dispatch_line(PyFrameObject* frame);
    virtual int dispatch_call(PyFrameObject* frame);
    virtual int dispatch_return(PyFrameObject* frame, PyObject* arg);
    virtual int dispatch_exception(PyFrameObject* frame, PyObject* arg);

    bool stop_here(PyFrameObject* frame) const;
    bool break_here(PyFrameObject* frame);
    bool is_cought(PyFrameObject* frame, PyObject* exception);
    bool break_anywhere(PyFrameObject* frame);

    void pause();
    void set_step();
    void set_next(PyFrameObject* frame);
    void set_return(PyFrameObject* frame);
    void set_continue();
    void set_quit();
    void set_break(const std::string& filename, line_t line, bool temporary = false, const std::string& cond = "");
    void set_exception_mode(exception_mode exmode);
};

bdb::exception_mode operator|(bdb::exception_mode lhs, bdb::exception_mode rhs);
bool operator&(bdb::exception_mode lhs, bdb::exception_mode rhs);