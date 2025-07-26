#include "bdb.h"
#include <print>
#include <filesystem>
#include <string_view>
using namespace bf2py;

namespace {
    PyObject* quit_error = nullptr;

    // like PyCapsule (which doesn't exist in python 2.3.4)
    struct bf2PyDebugger : PyObject {
        bdb* debugger;
    };

    PyTypeObject bf2PyDebuggerType = {
        .ob_refcnt = 1,
        .tp_name = (char*)"bf2py.Debugger",
        .tp_basicsize = sizeof(bf2PyDebugger),
        .tp_flags = Py_TPFLAGS_DEFAULT
    };
}

void bdb::raiseException(const std::string& message)
{
    PyErr_SetString(quit_error, message.c_str());
}

bdb::bdb()
{

}

bdb::~bdb()
{
    disable_trace();
}

bool bdb::pyInit()
{
    if (quit_error == nullptr) {
        char name[] = "bf2.BdbQuit";
        quit_error = PyErr_NewException(name, nullptr, nullptr);
    }

    if (!quit_error) {
        std::println(stderr, "Failed to create BdbQuit exception");
    }

    static bool typeInitialized = false;
    if (!typeInitialized) {
        bf2PyDebuggerType.tp_call = [](PyObject* self, PyObject* args, PyObject* kwds) -> PyObject* {
            // the very first trace call is initiated via /Python/sysmodule.c/trace_trampoline
            // which has slightly more overhead than our own trace_dispatch and which works slightly different
            PyEval_SetTrace(
                [](PyObject* obj, PyFrameObject* frame, int event, PyObject* arg) -> int {
                    return static_cast<bf2PyDebugger*>(obj)->debugger->trace_dispatch(frame, event, arg);
                },
                self
			);

            // after the first call, python's trace will no longer ues the trace_trampoline, but instead our own function
            Py_RETURN_NONE;
        };

        typeInitialized = PyType_Ready(&bf2PyDebuggerType) == 0;
    }

    if (!typeInitialized) {
        std::println(stderr, "Failed to initialize bf2py debugger type");
    }

    return !!quit_error && typeInitialized;
}

bool bdb::enable_trace()
{
    if (!_pyDebugger) {
        auto* self = PyObject_NEW(bf2PyDebugger, &bf2PyDebuggerType);
        if (!self) {
            std::println(stderr, "Failed to create bf2PyDebugger instance");
			return false;
		}

        new (self) bf2PyDebugger();
        self->ob_refcnt = 1;
        self->ob_type = &bf2PyDebuggerType;
        self->ob_type->ob_refcnt++;
        self->debugger = this;

        _pyDebugger = self;
	}

	PyEval_SetTrace(
        [](PyObject* obj, PyFrameObject* frame, int event, PyObject* arg) -> int {
            return reinterpret_cast<bf2PyDebugger*>(obj)->debugger->trace_dispatch(frame, event, arg);
        },
        reinterpret_cast<PyObject*>(_pyDebugger)
    );

    return true;
}

bool bdb::enable_thread_trace()
{
    PyNewRef threadingModule = PyImport_ImportModule((char*)"threading");
    if (!threadingModule) {
        std::println(stderr, "Failed to import threading module");
        return false;
    }

    auto threadingDict = PyModule_GetDict(threadingModule);
    if (!threadingModule) {
        std::println(stderr, "Failed to get threading module dict");
        return false;
    }

    auto setTrace = PyDict_GetItemString(threadingDict, "settrace");
    if (!setTrace) {
        std::println(stderr, "Failed to get settrace from threading module");
        return false;
    }

	// register the callback which is implemented in bf2PyDebuggerType.tp_call
    // (the debugger object itself is callable)
    PyNewRef res = PyObject_CallFunction(setTrace, (char*)"O", _pyDebugger);
    if (!res) {
        std::println(stderr, "Failed to call threading.settrace");
    }

    return res;
}

void bdb::disable_trace()
{
    if (_pyDebugger) {
		Py_DECREF(_pyDebugger);
        _pyDebugger = nullptr;

        // the debugger might be destructed after Py_Finalize was called,
        // in this case we must not call any more Py* functions
        if (Py_IsInitialized()) {
            // disable trace on all previously traced threads
            for (auto interpreter = PyInterpreterState_Head(); interpreter; interpreter = PyInterpreterState_Next(interpreter)) {
                for (auto thread = PyInterpreterState_ThreadHead(interpreter); thread; thread = PyThreadState_Next(thread)) {
                    if (thread->c_traceobj && Py_TYPE(thread->c_traceobj) == &bf2PyDebuggerType) {
                        auto currThread = PyThreadState_Swap(thread);
                        PyEval_SetTrace(nullptr, nullptr);
                        PyThreadState_Swap(currThread);
                    }
                }
            }
        }
    }
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

void bdb::reset()
{
    _ignored_frames.clear();
    _step = false;
    _stopframe = nullptr;
    _returnframe = nullptr;
}

void bdb::pause()
{
    _ignored_frames.clear();
    set_step();
}

std::string bdb::canonic(const std::string& filename)
{
    if (filename.starts_with("<") && filename.ends_with(">")) {
        return filename;
    }

    auto iter = _fncache.find(filename);
    if (iter != _fncache.end()) {
        return iter->second;
    }

    auto res = _fncache.emplace(filename, normalize_path(filename));
    return res.first->second;
}

std::pair<std::deque<std::pair<PyFrameObject*, std::size_t>>, std::size_t> bdb::get_stack(PyFrameObject* frame, PyObject* _traceback)
{
    // traceback object is not public (traceback.c) and I don't want to bother with C-API calls to retrieve the attributes
    struct _tracebackobject {
        PyObject_HEAD
        struct _tracebackobject* tb_next;
        PyFrameObject* tb_frame;
        int tb_lasti;
        int tb_lineno;
    };

    auto traceback = reinterpret_cast<_tracebackobject*>(_traceback);
    std::deque<std::pair<PyFrameObject*, std::size_t>> stack;

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
    if (trace_ignore()) {
        // evaluating a breakpoint condition - always happens recuresively
        return 0;
    }

    _currentbp = nullptr;

    switch (event) {
    case PyTrace_CALL: return dispatch_call(frame);
    case PyTrace_EXCEPTION: return dispatch_exception(frame, arg);
    case PyTrace_LINE: return dispatch_line(frame);
    case PyTrace_RETURN: return dispatch_return(frame, arg);
    }

    return 0;
}

int bdb::dispatch_line(PyFrameObject* frame)
{
    if (_ignored_frames.contains(frame)) {
        return 0;
    }

    if (stop_here(frame) || break_here(frame)) {
        user_line(frame);

        if (_quitting) {
            raiseException("quitting");
            return -1;
        }
    }

    return 0;
}

int bdb::dispatch_call(PyFrameObject* frame)
{
    if (frame->f_back == nullptr) {
        user_entry(frame);
    }
    else if (stop_here(frame) || break_anywhere(frame)) {
        user_call(frame);
    }
    else {
        _ignored_frames.insert(frame);
        return 0;
    }

	if (_quitting) {
		raiseException("quitting");
		return -1;
	}

    return 0;
}

int bdb::dispatch_return(PyFrameObject* frame, PyObject* arg)
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

    auto it = _ignored_frames.find(frame);
    if (it != _ignored_frames.end()) {
        _ignored_frames.erase(it);
        return 0;
    }

    if (frame == _returnframe || stop_here(frame)) {
        user_return(frame, arg);
        if (_stopframe == frame) {
            // cannot stop on this frame again, so stop on parent frame
            _stopframe = frame->f_back;
        }

        if (_quitting) {
			raiseException("quitting");
			return -1;
        }
    }

    return 0;
}

int bdb::dispatch_exception(PyFrameObject* frame, PyObject* exec)
{
    if (
        (_exmode & exception_mode::ALL_EXCEPTIONS)
        || (_exmode & exception_mode::UNHANDLED_EXCEPTION && !is_cought(frame, exec))
        || stop_here(frame)
        ) {
        user_exception(frame, exec);

        if (_quitting) {
            raiseException("quitting");
            return -1;
        }
    }

    return 0;
}

bool bdb::stop_here(PyFrameObject* frame) const
{
    if (_step || frame == _stopframe) {
        return true;
    }

    return false;
}

bool bdb::break_here(PyFrameObject* frame)
{
    const auto filename = canonic(PyString_AsString(frame->f_code->co_filename));
    auto breakIter = _breaks.find(filename); 
    if (breakIter == _breaks.end()) {
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
            _evaling = true;
            const auto val = PyRun_String(bp->condition.c_str(), Py_eval_input, frame->f_globals, frame->f_locals);
            _evaling = false;
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

        _currentbp = &(*bp);
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
    auto contains = _breaks.contains(filename);
    return contains;
}

void bdb::set_step()
{
    // exceptions can also occur in ignored frames, in this case "step into" must clear those
    _ignored_frames.clear();
    _step = true;
    _returnframe = nullptr;
    _quitting = false;
}

void bdb::set_next(PyFrameObject* frame)
{
    _step = false;
    _stopframe = frame;
    _returnframe = nullptr;
    _quitting = false;
}

void bdb::set_return(PyFrameObject* frame)
{
    _step = false;
    _stopframe = nullptr;
    _returnframe = frame;
    _quitting = false;
}

void bdb::set_continue()
{
    _step = false;
    _stopframe = nullptr;
    _returnframe = nullptr;
    _quitting = false;
}

void bdb::set_quit()
{
    reset();
    _quitting = true;
}

void bdb::set_break(const std::string& _filename, line_t line, bool temporary, const std::string& cond)
{
    const auto filename = canonic(_filename);
    _breaks[filename][line].emplace_back(filename, line, temporary, cond);
}

void bdb::set_exception_mode(exception_mode exmode)
{
    _exmode = exmode;
}

bdb::exception_mode operator|(bdb::exception_mode lhs, bdb::exception_mode rhs) {
    using exception_t = std::underlying_type<bdb::exception_mode>::type;
    return bdb::exception_mode(static_cast<exception_t>(lhs) | static_cast<exception_t>(rhs));
}

bool operator&(bdb::exception_mode lhs, bdb::exception_mode rhs) {
    using exception_t = std::underlying_type<bdb::exception_mode>::type;
    return bdb::exception_mode(static_cast<exception_t>(lhs) & static_cast<exception_t>(rhs)) != bdb::exception_mode::NEVER;
}