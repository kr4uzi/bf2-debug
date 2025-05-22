#include "debugger.h"
#include <thread>
#include <dap/io.h>
#include <dap/protocol.h>
#include <print>

void Event::wait() {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return fired; });
}

void Event::fire() {
    std::unique_lock lock(mutex);
    fired = true;
    cv.notify_all();
}

debugger::debugger(bool stopOnEntry)
	: thread_id(std::hash<std::thread::id>{}(std::this_thread::get_id())), stopOnEntry(stopOnEntry), last_source_id(1)
{
	server = dap::net::Server::create();
	if (!server) {
		std::println("Failed to create server");
		return;
	}
}

void debugger::setHostModule(const decltype(hostModule)& _hostModule)
{
    std::unique_lock lock(mutex);
    hostModule = _hostModule;
    // any session already configured hasn't yet received the mod path
    send_mod_path();
}

void debugger::stop()
{
    server->stop();
}

void debugger::user_entry(std::unique_lock<std::mutex>& lock, PyFrameObject* frame)
{
    if (stopOnEntry) {
        statecv.wait(lock, [&] { return !sessions.empty(); });
        stopOnEntry = false;

        dap::StoppedEvent event;
        event.reason = "entry";
        event.threadId = thread_id;
        send(event);

        interaction(lock, frame, nullptr);
    }
}

void debugger::user_call(std::unique_lock<std::mutex>& lock, PyFrameObject* frame)
{
    if (stop_here(frame)) {
        dap::StoppedEvent event;
        event.threadId = thread_id;
        event.reason = "step";
        send(event);

        interaction(lock, frame, nullptr);
    }
}

void debugger::user_line(std::unique_lock<std::mutex>& lock, PyFrameObject* frame)
{
    dap::StoppedEvent event;
    event.threadId = thread_id;
    event.reason = "step";
	send(event);

    interaction(lock, frame, nullptr);
}

void debugger::user_return(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg)
{
    if (frame->f_locals) {
        PyDict_SetItemString(frame->f_locals, "__return__", arg);
    }

    dap::StoppedEvent event;
	event.threadId = thread_id;
    event.reason = "step";
    send(event);
    interaction(lock, frame, nullptr);
}

void debugger::user_exception(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg)
{
    PyObject* type = PyTuple_GET_ITEM(arg, 0);
    PyObject* value = PyTuple_GET_ITEM(arg, 1);
    PyObject* traceback = PyTuple_GET_ITEM(arg, 2);
    PyObject* valueRepr = PyObject_Repr(value);
    PyObject* typeStr = PyObject_Str(type);

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
        dap::StoppedEvent event;
        event.threadId = thread_id;
        event.reason = "exception";
        event.text = std::format("{}: {}", PyString_AsString(typeStr), PyString_AsString(valueRepr));
        send(event);
    }

    Py_XDECREF(typeStr);
    Py_XDECREF(valueRepr);

    interaction(lock, frame, traceback);
}

void debugger::on_breakpoint_error(Breakpoint& bp, const std::string& message)
{
    log(std::format("breakpoint eval error: {}\n", message));
}

void debugger::forget()
{
    var_refs.clear();
	source_refs.clear();
	stack.clear();
	curindex = 0;
	curframe = nullptr;
}

void debugger::setup(PyFrameObject* frame, PyObject* traceback)
{
    forget();
	std::tie(stack, curindex) = get_stack(frame, traceback);
	curframe = stack[curindex].first;
}

void debugger::interaction(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* traceback)
{
    if (!sessions.empty()) {
	    setup(frame, traceback);
        state = Status::Stopped;
        statecv.wait(lock, [&] { return state == Status::Running; });
        forget();
    }
}

void debugger::do_clear(Breakpoint& bp)
{

}

void debugger::log(const std::string& msg)
{
    dap::OutputEvent event;
	event.category = "console";
	event.output = msg;
    send(event);
}