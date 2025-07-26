#include "debugger.h"
#include <print>
using namespace bf2py;

int debugger::trace_dispatch(PyFrameObject* frame, int event, PyObject* arg)
{
    if (trace_ignore()) {
        // when evaluating (e.g. a breakpoint condition) or quitting, there is no need for any additional overhead
        return bdb::trace_dispatch(frame, event, arg);
    }

    return asio::post(_ctx, asio::use_future([&] {
        return bdb::trace_dispatch(frame, event, arg);
    })).get();
}

void debugger::setHostModule(const decltype(_hostModule)& hostModule)
{
    _hostModule = hostModule;

    if (_session) {
        auto it = _hostModule.find("sgl_getModDirectory");
        if (it != _hostModule.end()) {
            auto modDir = it->second(nullptr, nullptr);
            _session->send_modpath(std::format("{};{}", std::filesystem::current_path().string(), PyString_AS_STRING(modDir)));
        }
    }
}

void debugger::start()
{
    asio::co_spawn(_ctx, run(), asio::detached);
    start_io_runner();
}

void debugger::stop()
{
    _ctx.stop();
}

asio::awaitable<void> debugger::run()
{
    asio::ip::tcp::acceptor acceptor{ _ctx, asio::ip::tcp::endpoint{ asio::ip::make_address_v4("127.0.0.1"), _port}};
    while (acceptor.is_open()) {
        auto [error, socket] = co_await acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
        if (error)
            break;

        // only one session at a time
        _session.emplace(*this, std::move(socket));
        co_await _session->run();
    }
}

void debugger::start_io_runner()
{
    _io_runner = std::jthread([&](std::stop_token token) {
        while (!token.stop_requested() && !_ctx.stopped()) {
            _ctx.run_one();
        }
    });
}

void debugger::user_entry(PyFrameObject* frame)
{
    if (_wait_for_connection) {
        std::println("[debugger] waiting for session to connect on port {} ...", _port);

        while (!_session || !_session->initialized()) {
			_ctx.run_one();
        }

        _wait_for_connection = false;
        _session->send_entry(frame->f_tstate->thread_id);
        interaction(frame, nullptr);
    }
}

void debugger::user_call(PyFrameObject* frame)
{
    if (!_session) {
        return;
    }
    
    if (stop_here(frame)) {
		_session->send_step(frame->f_tstate->thread_id);
        interaction(frame, nullptr);
    }
}

void debugger::user_line(PyFrameObject* frame)
{
    if (!_session) {
        return;
    }

    _session->send_step(frame->f_tstate->thread_id);
    interaction(frame, nullptr);
}

void debugger::user_return(PyFrameObject* frame, PyObject* returnValue)
{
    if (!_session) {
        return;
    }

    if (frame->f_locals && returnValue) {
        PyDict_SetItemString(frame->f_locals, "__return__", returnValue);
    }

    _session->send_step(frame->f_tstate->thread_id);
    interaction(frame, nullptr);
}

void debugger::user_exception(PyFrameObject* frame, PyObject* excInfo)
{
    auto type = PyTuple_GET_ITEM(excInfo, 0);
    auto value = PyTuple_GET_ITEM(excInfo, 1);
    auto traceback = PyTuple_GET_ITEM(excInfo, 2);
    PyNewRef valueRepr = PyObject_Repr(value);
    PyNewRef typeStr = PyObject_Str(type);

    if (!frame->f_locals) {
        PyFrame_FastToLocals(frame);
    }

    if (frame->f_locals) {
        auto tuple = PyTuple_New(2);
        if (tuple) {
            PyTuple_SET_ITEM(tuple, 0, type);
            PyTuple_SET_ITEM(tuple, 1, value);
            PyDict_SetItemString(frame->f_locals, "__exception__", tuple);
            Py_DECREF(tuple);
        }
    }

    if (valueRepr && typeStr) {
        _session->send_exception(frame->f_tstate->thread_id, std::format("{}: {}", PyString_AsString(typeStr), PyString_AsString(valueRepr)));
    }

    interaction(frame, traceback);
}

void debugger::on_breakpoint_error(Breakpoint& bp, const std::string& message)
{
    log(std::format("breakpoint eval error: {}\n", message));
}

void debugger::interaction(PyFrameObject* frame, PyObject* traceback)
{
    setup(frame, traceback);
    _state = Status::Stopped;
    
    while (_state == Status::Stopped) {
        _ctx.run_one();
    }

    forget();
}

void debugger::setup(PyFrameObject* frame, PyObject* traceback)
{
    forget();
    std::tie(_stack, _curindex) = get_stack(frame, traceback);
    _curframe = _stack[_curindex].first;
    _curthread = frame->f_tstate->thread_id;
}

void debugger::forget()
{
	if (_session) {
		_session->forget(_curthread);
	}

    _stack.clear();
    _curindex = 0;
    _curframe = nullptr;
    _curthread = -1;
}

void debugger::do_clear(Breakpoint& bp)
{

}

void debugger::log(const std::string& msg)
{
    if (_session) {
        _session->send_output(msg);
    }
}

void debugger::log(const std::u8string& msg)
{
    if (_session) {
        _session->send_output(msg);
    }
}