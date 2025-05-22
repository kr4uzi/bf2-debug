#include "bdb.h"
#include <print>
#include <filesystem>
#include <string_view>

PyObject* (*frame_getlineno)(PyFrameObject*, void*);
PyObject* quitError = nullptr;
thread_local bdb* thread_debugger = nullptr;

bool bdb::pyInit()
{
    static bool initialized = false;
    if (initialized) {
        return true;
    }

    if (!quitError) {
        char exceptionName[] = "bf2.BdbQuit";
        quitError = PyErr_NewException(exceptionName, nullptr, nullptr);
        if (!quitError) {
            std::println(stderr, "Failed to create BdbQuit exception");
        }
    }

    if (!frame_getlineno) {
        using namespace std::string_view_literals;
        for (auto getset = PyFrame_Type.tp_getset; getset && getset->name != nullptr; ++getset) {
            if (getset->name == "f_lineno"sv) {
                frame_getlineno = reinterpret_cast<decltype(frame_getlineno)>(getset->get);
                break;
            }
        }
    }

    initialized = quitError && frame_getlineno;
    return initialized;
}

void raiseException(const std::string& message)
{
    PyErr_SetString(quitError, message.c_str());
}

bdb::~bdb()
{
    disable_trace();
}

void bdb::enable_trace()
{
    if (thread_debugger) {
        throw std::runtime_error("enable_trace can only be called once per thread!");
    }

    thread_debugger = this;

    // as we only trace a single thread, we only have once trace function and can therefore
    // save the conversion cost of PyCObject_FromVoidPtr and PyCObject_AsVoidPtr
    PyEval_SetTrace([](PyObject* obj, PyFrameObject* frame, int event, PyObject* arg) -> int {
        (void)obj;
        return thread_debugger->trace_dispatch(frame, event, arg);
    }, nullptr);
}

void bdb::disable_trace()
{
    thread_debugger = nullptr;
}

std::string bdb::normalize_path(const std::string& filename)
{
	if (filename.starts_with("<") && filename.ends_with(">")) {
		return filename;
	}

	auto path = std::filesystem::absolute(filename).string();
	std::transform(path.begin(), path.end(), path.begin(), [](auto c) { return std::tolower(c); });
	return path;
}

decltype(PyFrameObject::f_lineno) bdb::frame_lineno(PyFrameObject* frame)
{
    auto lineno = frame_getlineno(frame, nullptr);
    return PyInt_AS_LONG(lineno);
}

void bdb::reset()
{
    ignored_frames.clear();
    step = false;
    stopframe = nullptr;
    returnframe = nullptr;
}

void bdb::pause()
{
    ignored_frames.clear();
    set_step();
}

std::string bdb::canonic(const std::string& filename)
{
    if (filename.starts_with("<") && filename.ends_with(">")) {
        return filename;
    }

    auto iter = fncache.find(filename);
    if (iter != fncache.end()) {
        return iter->second;
    }

    auto res = fncache.emplace(filename, normalize_path(filename));
    return res.first->second;
}

std::pair<std::deque<std::pair<PyFrameObject*, std::size_t>>, std::size_t> bdb::get_stack(PyFrameObject* frame, PyObject* _traceback)
{
    struct _tracebackobject {
        PyObject_HEAD
        struct _tracebackobject* tb_next;
        PyFrameObject* tb_frame;
        int tb_lasti;
        int tb_lineno;
    };

    auto traceback = reinterpret_cast<_tracebackobject*>(_traceback);
    std::deque<std::pair<PyFrameObject*, std::size_t>> stack;

    // traceback object is not public (traceback.c) and I don't want to bother with C-API calls
    if (traceback && frame == traceback->tb_frame) {
        traceback = traceback->tb_next;
    }

    while (frame) {
        stack.push_front({ frame, frame->f_lineno });
        frame = frame->f_back;
    }

    std::size_t i = stack.empty() ? 0 : stack.size() - 1;

    while (traceback) {
        stack.push_back({
            traceback->tb_frame,
            traceback->tb_lineno
        });
        traceback = traceback->tb_next;
    }

    return { stack, i };
}

int bdb::trace_dispatch(PyFrameObject* frame, int event, PyObject* arg)
{
    if (evaling) {
        // evaluating a breakpoint condition - always happens recuresively (and when locked)
        return 0;
    }

    std::unique_lock lock(mutex);
    currentbp = nullptr;
    if (quitting) {
        return 0;
    }

    switch (event) {
    case PyTrace_CALL: return dispatch_call(lock, frame);
    case PyTrace_EXCEPTION: return dispatch_exception(lock, frame, arg);
    case PyTrace_LINE: return dispatch_line(lock, frame);
    case PyTrace_RETURN: return dispatch_return(lock, frame, arg);
    }

    return 0;
}

int bdb::dispatch_line(std::unique_lock<std::mutex>& lock, PyFrameObject* frame)
{
    if (ignored_frames.contains(frame)) {
        return 0;
    }

    if (stop_here(frame) || break_here(frame)) {
        user_line(lock, frame);

        if (quitting) {
            raiseException("quitting");
            return -1;
        }
    }

    return 0;
}

int bdb::dispatch_call(std::unique_lock<std::mutex>& lock, PyFrameObject* frame)
{
    if (frame->f_back == nullptr) {
        user_entry(lock, frame);
    }
    else if (stop_here(frame) || break_anywhere(frame)) {
        user_call(lock, frame);
    }
    else {
        ignored_frames.insert(frame);
        return 0;
    }

	if (quitting) {
		raiseException("quitting");
		return -1;
	}

    return 0;
}

int bdb::dispatch_return(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg)
{
    // scope_exit doesn't exist yet :(
    // if the mainframe returns, no saved reference remains valid
    // the reset must only happen after the scope exists, as we still need to check if we have to break
    auto resetter = [&](void*) {
        if (frame->f_back == nullptr) {
            // return from the main frame = end of the program
            reset();
        }
    };
    auto reset_guard = std::unique_ptr<void, decltype(resetter)>(nullptr, resetter);

    auto it = ignored_frames.find(frame);
    if (it != ignored_frames.end()) {
        ignored_frames.erase(it);
        return 0;
    }

    if (frame == returnframe || stop_here(frame)) {
        user_return(lock, frame, arg);
        if (stopframe == frame) {
            // cannot stop on this frame again, so stop on parent frame
            stopframe = frame->f_back;
        }

        if (quitting) {
			raiseException("quitting");
			return -1;
        }
    }

    return 0;
}

int bdb::dispatch_exception(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* exec)
{
    if (
        (exmode & exception_mode::ALL_EXCEPTIONS)
        || (exmode & exception_mode::UNHANDLED_EXCEPTION && !is_cought(frame, exec))
        || stop_here(frame)
        ) {
        user_exception(lock, frame, exec);

        if (quitting) {
            raiseException("quitting");
            return -1;
        }
    }

    return 0;
}

bool bdb::stop_here(PyFrameObject* frame) const
{
    if (step || frame == stopframe) {
        return true;
    }

    return false;
}

bool bdb::break_here(PyFrameObject* frame)
{
    const auto filename = canonic(PyString_AsString(frame->f_code->co_filename));
    auto breakIter = breaks.find(filename); 
    if (breakIter == breaks.end()) {
        return false;
    }

    // f_lineno has a getter in PyFrameObject's tp_getset 
    auto lineno = frame->f_lineno;
    if (lineno < 0) {
        return false;
    }

	auto& fileBreaks = breakIter->second;
	auto lineBreaksIter = fileBreaks.find(lineno);
    if (lineBreaksIter == fileBreaks.end()) {
        return false;
    }

    // check for effective breakpoint
    auto& breaks = lineBreaksIter->second;
    std::size_t i = 0;
    for (auto bp = breaks.begin(), end = breaks.end(); bp != end; ++bp) {
        ++i;
        if (!bp->enabled) {
            continue;
        }

        // Count every hit when bp is enabled
        bp->hits++;
        if (!bp->condition.empty()) {
            // Conditional bp.
            // Ignore count applies only to those bpt hits where the
            // condition evaluates to true.
            evaling = true;
            const auto val = PyRun_String(bp->condition.c_str(), Py_eval_input, frame->f_globals, frame->f_locals);
            evaling = false;
            if (!val) {
                // if eval fails, most conservative
                // thing is to stop on breakpoint
                // regardless of ignore count.
                // Don't delete temporary,
                // as another hint to user.
                on_breakpoint_error(*bp, std::format("Error evaluating condition: {}", bp->condition));
                return false;
            }

            const auto isTrue = PyObject_IsTrue(val);
            if (isTrue == -1) {
                std::println(stderr, "Error casting value to true: {}", bp->condition);
                return false;
            }

            if (isTrue == 0) {
                continue;
            }
        }

        if (bp->ignore > 0) {
            --bp->ignore;
            continue;
        }

        if (bp->temporary) {
            // Temporary breakpoints are deleted after first hit
            // and not re-enabled.
            //bp->enabled = false;
            do_clear(*bp);
        }

        currentbp = &(*bp);
        return true;
    }

    return false;
}

bool bdb::is_cought(PyFrameObject* frame, PyObject* exec)
{
    for (auto f = frame; f; f = f->f_back) {
        if (f->f_iblock > 0) {
            return true;
        }
    }

    return false;
}

bool bdb::break_anywhere(PyFrameObject* frame)
{
    const auto filename = canonic(PyString_AsString(frame->f_code->co_filename));
    auto contains = breaks.contains(filename);
    return contains;
}

void bdb::set_step()
{
    // exceptions can also occur in ignored frames, in this case "step into" must clear those
    ignored_frames.clear();
    step = true;
    returnframe = nullptr;
    quitting = false;
}

void bdb::set_next(PyFrameObject* frame)
{
    step = false;
    stopframe = frame;
    returnframe = nullptr;
    quitting = false;
}

void bdb::set_return(PyFrameObject* frame)
{
    step = false;
    stopframe = nullptr;
    returnframe = frame;
    quitting = false;
}

void bdb::set_continue()
{
    step = false;
    stopframe = nullptr;
    returnframe = nullptr;
    quitting = false;
}

void bdb::set_quit()
{
    reset();
    PyEval_SetTrace(nullptr, nullptr);
}

void bdb::set_break(const std::string& _filename, line_t line, bool temporary, const std::string& cond)
{
    const auto filename = canonic(_filename);
    breaks[filename][line].emplace_back(filename, line, temporary, cond);
}

void bdb::set_exception_mode(exception_mode _exmode)
{
    exmode = _exmode;
}

bdb::exception_mode operator|(bdb::exception_mode lhs, bdb::exception_mode rhs) {
    using exception_t = std::underlying_type<bdb::exception_mode>::type;
    return bdb::exception_mode(static_cast<exception_t>(lhs) | static_cast<exception_t>(rhs));
}

bool operator&(bdb::exception_mode lhs, bdb::exception_mode rhs) {
    using exception_t = std::underlying_type<bdb::exception_mode>::type;
    return bdb::exception_mode(static_cast<exception_t>(lhs) & static_cast<exception_t>(rhs)) != bdb::exception_mode::NEVER;
}