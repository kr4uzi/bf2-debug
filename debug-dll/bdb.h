#pragma once
#include "python.h"
#include "breakpoint.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>
#include <set>
#include <optional>

struct bottom_frame
{
    PyFrameObject* value = nullptr;

    operator PyFrameObject* () const noexcept { return value; }
    PyFrameObject* operator=(PyFrameObject* rhs) {
        value = rhs;
        return rhs;
    }
};

class bdb
{
    std::set<PyFrameObject*> ignored_frames;
    PyFrameObject* stopframe = nullptr;
    bool step = false;
    PyFrameObject* returnframe = nullptr;
    std::unordered_map<std::string, std::string> fncache;
    bool evaling = false;

public:
    using line_t = Breakpoint::line_t;

    static bool pyInit();
	static std::string normalize_path(const std::string& filename);
    static decltype(PyFrameObject::f_lineno) frame_lineno(PyFrameObject* frame);

    enum class exception_mode : unsigned char {
        NEVER = 0,
        UNHANDLED_EXCEPTION = 1,
        HANDLED_EXCEPTION = 2,
        ALL_EXCEPTIONS = 3
    };

protected:
	std::mutex mutex;
    bool quitting = false;
    Breakpoint* currentbp = nullptr;
    std::unordered_map<std::string, std::unordered_map<line_t, std::vector<Breakpoint>>> breaks;
    exception_mode exmode = exception_mode::NEVER;

protected:
	virtual void user_entry(std::unique_lock<std::mutex>& lock, PyFrameObject* frame) = 0;
    virtual void user_call(std::unique_lock<std::mutex>& lock, PyFrameObject* frame) = 0;
    virtual void user_line(std::unique_lock<std::mutex>& lock, PyFrameObject* frame) = 0;
    virtual void user_return(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg) = 0;
    virtual void user_exception(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg) = 0;
    virtual void do_clear(Breakpoint& bp) = 0;
    virtual void on_breakpoint_error(Breakpoint& bp, const std::string& msg) = 0;

    void reset();
    void pause();
    std::string canonic(const std::string& filename);
	std::pair<std::deque<std::pair<PyFrameObject*, std::size_t>>, std::size_t> get_stack(PyFrameObject* frame, PyObject* traceback);

public:
    ~bdb();
    void enable_trace();
    void disable_trace();

    int trace_dispatch(PyFrameObject* frame, int event, PyObject* arg);
    int dispatch_line(std::unique_lock<std::mutex>& lock, PyFrameObject* frame);
    int dispatch_call(std::unique_lock<std::mutex>& lock, PyFrameObject* frame);
    int dispatch_return(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg);
    int dispatch_exception(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg);

    bool stop_here(PyFrameObject* frame) const;
    bool break_here(PyFrameObject* frame);
    bool is_cought(PyFrameObject* frame, PyObject* exception);
    bool break_anywhere(PyFrameObject* frame);

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