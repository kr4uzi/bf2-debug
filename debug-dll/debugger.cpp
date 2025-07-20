#include "debugger.h"
#include <print>
#define __STDC_WANT_LIB_EXT1__ 1
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <debugapi.h>
#define close _close
#define read _read
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define pipe _pipe
#else
#include <unistd.h>
#include <syslog.h>
#endif

debugger::debugger(bool stopOnEntry, asio::ip::port_type port)
    : _wait_for_connection(stopOnEntry), _port(port)
{

}

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

void show_error(const std::string& user_message)
{
    char errmsg[2048];
    strerror_s(errmsg, errno);

    auto message = std::format("{}: {} {}", user_message, errno, errmsg);
#ifdef _WIN32
	OutputDebugStringA(message.c_str());
#else
	syslog(LOG_ERR, "%s: %s", message.c_str(), strerror(errno));
#endif
    
	std::println("{}", message);
}

void debugger::start_redirect_output()
{
    if (_original_stdout_fd != -1) {
        return;
	}

    _original_stdout_fd = dup(fileno(stdout));
    if (_original_stdout_fd == -1) {
        show_error("dup failed");
        return;
	}

#ifdef _WIN32
    if (pipe(_output_fd, 4096, _O_BINARY) == -1) {
#else
    if (pipe(_output_fd) {
#endif
        show_error("pipe failed");
        stop_redirect_output();
        return;
    }

    if (dup2(_output_fd[1], fileno(stdout))) {
        show_error("pipe failed");
        stop_redirect_output();
        return;
    }

    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) {
        show_error("setvbuf failed");
        stop_redirect_output();
        return;
    }

    _output_redirector = std::jthread([this](std::stop_token token) {
        std::string line;
        char buffer[4096];
        while (!token.stop_requested()) {
            auto bytesRead = read(_output_fd[0], buffer, sizeof(buffer));
            if (bytesRead <= 0) {
                break;
            }

            line += std::string(buffer, bytesRead);

            for (auto newLinePos = line.find('\n'); newLinePos != std::string::npos; newLinePos = line.find('\n')) {
                auto output = line.substr(0, newLinePos);
                line.erase(0, newLinePos + 1);

                if (output.empty()) {
                    continue;
				}

                if (_session) {
                    _session->send_output(output);
                }

#ifdef _WIN32
                OutputDebugStringA(output.c_str());
#else
                syslog(LOG_DEBUG, "%s", buffer.c_str());
#endif
            }
        }

        close(_output_fd[0]);
    });
}

void debugger::stop_redirect_output()
{
    // close write pipe
    if (_output_fd[1] != -1) {
        if (close(_output_fd[1]) == -1) {
            show_error("close failed (_output_fd[1])");
		}

		_output_fd[1] = -1;
    }

	// restore stdout
    if (_original_stdout_fd != -1) {
        if (dup2(_original_stdout_fd, fileno(stdout)) == -1) {
            show_error("dup2 failed");
		} else {
            if (close(_original_stdout_fd) == -1) {
                show_error("close failed (_original_stdout_fd)");
            }
        }

        _original_stdout_fd = -1;
	}

	// stop output redirector thread
    if (_output_redirector.joinable()) {
        _output_redirector.request_stop();
        _output_redirector.join();
    }
    
    // close read pipe (if not done by redirector thread - e.g. if start_redirect_output failed partially)
    if (_output_fd[0] != -1) {
        if ( close(_output_fd[0]) == -1) {
            show_error("close failed (_output_fd[0])");
		}

	    _output_fd[0] = -1;
	}
}

void debugger::user_entry(PyFrameObject* frame)
{
    if (_wait_for_connection) {
        std::println("Waiting for debugger to attach on {} ...", _port);

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

void debugger::user_return(PyFrameObject* frame, PyObject* arg)
{
    if (!_session) {
        return;
    }

    if (frame->f_locals) {
        PyDict_SetItemString(frame->f_locals, "__return__", arg);
    }

    _session->send_step(frame->f_tstate->thread_id);
    interaction(frame, nullptr);
}

void debugger::user_exception(PyFrameObject* frame, PyObject* arg)
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
        _session->send_exception(frame->f_tstate->thread_id, std::format("{}: {}", PyString_AsString(typeStr), PyString_AsString(valueRepr)));
    }

    Py_XDECREF(typeStr);
    Py_XDECREF(valueRepr);

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