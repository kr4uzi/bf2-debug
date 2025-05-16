#include "bdb.h"
#include <print>
#include <filesystem>

PyObject* quitError = nullptr;
bool bdb::pyInit()
{
    if (quitError) {
        return true;
    }

	char exceptionName[] = "bf2.BdbQuit";
	quitError = PyErr_NewException(exceptionName, nullptr, nullptr);
	if (!quitError) {
		std::println(stderr, "Failed to create BdbQuit exception");
		return false;
	}
	
    return true;
}

void raiseException(const std::string& message)
{
    PyErr_SetString(quitError, message.c_str());
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

    // traceback object is not public (traceback.c)
    if (traceback && frame == traceback->tb_frame) {
        traceback = traceback->tb_next;
    }

    while (frame) {
        stack.push_front({ frame, PyCode_Addr2Line(frame->f_code, frame->f_lasti) /* frame->f_lineno */ });
        if (frame == botframe) {
            break;
        }

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
    static bool first = true;
    if (first) {
        first = false;
		entry(frame);
        return 0;
    }


    currentbp = nullptr;
    if (quitting) {
        return 0;
    }
    else if (event == PyTrace_CALL) {
        return dispatch_call(frame);
    }
    
    if (frame->f_trace) {
        return 0;
    }

    switch (event) {
    case PyTrace_LINE: return dispatch_line(frame);
    case PyTrace_CALL: return dispatch_call(frame);
    case PyTrace_RETURN: return dispatch_return(frame, arg);
    case PyTrace_EXCEPTION: return dispatch_exception(frame, arg);
    }

    return 0;
}

int bdb::dispatch_line(PyFrameObject* frame)
{
    if (stop_here(frame) || break_here(frame)) {
        user_line(frame);
        if (quitting) {
            raiseException("quitting");
            return -1;
        }
    }

    return 0;
}

int bdb::dispatch_call(PyFrameObject* frame)
{
    //if (botframe == nullptr) {
    //    // First call of dispatch since reset()
    //    auto first = frame->f_back ? frame->f_back : frame;
    //    botframe = first;
    //    return 0;
    //}

    if (!(stop_here(frame) || break_anywhere(frame))) {
        Py_INCREF(Py_None);
        frame->f_trace = Py_None;
        return 0;
    }

    user_call(frame);
	if (quitting) {
		raiseException("quitting");
		return -1;
	}

    return 0;
}

int bdb::dispatch_return(PyFrameObject* frame, PyObject* arg)
{
    if (stop_here(frame) || frame == returnframe) {
        user_return(frame, arg);
        if (quitting) {
			raiseException("quitting");
			return -1;
        }
    }

    return 0;
}

int bdb::dispatch_exception(PyFrameObject* frame, PyObject* arg)
{
    if (stop_here(frame)) {
        user_exception(frame, arg);
        if (quitting) {
			raiseException("quitting");
			return -1;
        }
    }

    return 0;
}

bool bdb::stop_here(PyFrameObject* frame) const
{
    if (frame == stopframe) {
        return true;
    }

    while (frame && frame != stopframe) {
        if (frame == botframe) {
            return true;
        }

        frame = frame->f_back;
    }

    return false;
}

bool bdb::break_here(PyFrameObject* frame)
{
    std::unique_lock lock(mutex);
    const auto filename = canonic(PyString_AsString(frame->f_code->co_filename));
    auto breakIter = breaks.find(filename); 
    if (breakIter == breaks.end()) {
        return false;
    }

    // f_lineno has a getter in PyFrameObject's tp_getset which 
    auto lineno = PyCode_Addr2Line(frame->f_code, frame->f_lasti); // frame->f_lineno;
    if (lineno < 0) {
        return false;
    }

	auto& fileBreaks = breakIter->second;
    if (!fileBreaks.contains(lineno)) {
        // The line itself has no breakpoint, but maybe the line is the
        // first line of a function with breakpoint set by function name.
        lineno = frame->f_code->co_firstlineno;
    }

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
            const auto val = PyRun_String(bp->condition.c_str(), Py_eval_input, frame->f_globals, frame->f_locals);
            if (!val) {
                // if eval fails, most conservative
                // thing is to stop on breakpoint
                // regardless of ignore count.
                // Don't delete temporary,
                // as another hint to user.
                std::println(stderr, "Error evaluating condition: {}", bp->condition);
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

bool bdb::break_anywhere(PyFrameObject* frame)
{
    const auto filename = canonic(PyString_AsString(frame->f_code->co_filename));
    auto contains = breaks.contains(filename);
    return contains;
}

void bdb::set_step()
{
    stopframe = nullptr;
    returnframe = nullptr;
    quitting = false;
}

void bdb::set_next(PyFrameObject* frame)
{
    stopframe = frame;
    returnframe = nullptr;
    quitting = false;
}

void bdb::set_return(PyFrameObject* frame)
{
    stopframe = frame->f_back;
    returnframe = frame;
    quitting = false;
}

void bdb::set_continue()
{
    stopframe = botframe;
    returnframe = nullptr;
    quitting = false;
}

void bdb::set_quit()
{
    stopframe = botframe;
    returnframe = nullptr;
    quitting = true;
    PyEval_SetTrace(nullptr, nullptr);
}

void bdb::set_break(const std::string& _filename, line_t line, bool temporary, const std::string& cond)
{
    const auto filename = canonic(_filename);
    breaks[filename][line].emplace_back(filename, line, temporary, cond);
}