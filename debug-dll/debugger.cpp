#include "debugger.h"
#include "dis.h"
#include <thread>
#include <dap/io.h>
#include <dap/protocol.h>
#include <dap/session.h>
#include <print>
#include <algorithm>
#include <libzippp/libzippp.h>
#include <Windows.h>
#include <chrono>

void Event::wait() {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return fired; });
    fired = false;
}

void Event::fire() {
    std::unique_lock<std::mutex> lock(mutex);
    fired = true;
    cv.notify_all();
}

debugger::debugger(bool stopOnEntry)
	: thread_id(std::hash<std::thread::id>{}(std::this_thread::get_id())), stopOnEntry(stopOnEntry)
{
	server = dap::net::Server::create();
	if (!server) {
		std::println("Failed to create server");
		return;
	}
}

void debugger::start(int port)
{
    auto onClientConnected = [this](const std::shared_ptr<dap::ReaderWriter>& socket) {
		std::hash<PyObject*> pyObjectHash{};
        std::hash<std::string> filenameHash{};
        Event terminate;

        std::shared_ptr<dap::Session> session{ dap::Session::create() };
        session->onError([&](const char* msg) {
            std::println("Session error: {}", msg);
            terminate.fire();
        });

        session->bind(socket);

        session->registerHandler([&](const dap::InitializeRequest&) {
            dap::InitializeResponse response;
            response.supportsConfigurationDoneRequest = true;
            // 'never', 'always', 'unhandled', 'userUnhandled'
            dap::array <dap::ExceptionBreakpointsFilter> exFilters;
            exFilters.push_back(dap::ExceptionBreakpointsFilter{
                .filter = "never",
                .label = "Never"
            });
            exFilters.push_back(dap::ExceptionBreakpointsFilter{
                .filter = "always",
                .label = "Alawys"
            });
            response.exceptionBreakpointFilters = exFilters;
            return response;
        });

        session->registerSentHandler([&](const dap::ResponseOrError<dap::InitializeResponse>&) {
            std::unique_lock sessionLock(mutex);
            session->send(dap::InitializedEvent());
            sessions.push_back(session);
            statecv.notify_all();
        });

        session->registerHandler([&](const dap::AttachRequest&) {
            std::unique_lock lock(mutex);
            while (state != Status::Stopped) {
                statecv.wait_for(lock, std::chrono::milliseconds(50));
            }

			return dap::AttachResponse();
		});

        session->registerHandler([&](const dap::ConfigurationDoneRequest&) {
            return dap::ConfigurationDoneResponse();
        });

        session->registerHandler([&](const dap::ThreadsRequest&) {
            dap::ThreadsResponse response;
            dap::Thread thread;
            thread.id = thread_id;
            thread.name = "bf2";
            response.threads.push_back(thread);
            return response;
        });

        session->registerHandler([&](const dap::ScopesRequest& request) -> dap::ResponseOrError<dap::ScopesResponse> {
			std::unique_lock lock(mutex);
			if (request.frameId >= stack.size()) {
				return dap::Error("Invalid frameId '%d'", int(request.frameId));
			}

			const auto& pyFrame = stack[static_cast<std::size_t>(request.frameId)].first;
			dap::ScopesResponse response;

            if (pyFrame->f_locals == nullptr) {
                PyFrame_FastToLocals(pyFrame);
            }

            if (pyFrame->f_locals) {
                dap::Scope locals;
                locals.name = "Locals";
                locals.presentationHint = "locals";
                locals.variablesReference = pyObjectHash(pyFrame->f_locals);
                var_refs[locals.variablesReference] = pyFrame->f_locals;
                response.scopes.push_back(locals);
            }

            if (pyFrame->f_globals) {
                dap::Scope globals;
                globals.name = "Globals";
                globals.variablesReference = pyObjectHash(pyFrame->f_globals);
                var_refs[globals.variablesReference] = pyFrame->f_globals;
                response.scopes.push_back(globals);
            }

            return response;
        });

        session->registerHandler([&](const dap::VariablesRequest& request) -> dap::ResponseOrError<dap::VariablesResponse> {
            std::unique_lock lock(mutex);
            dap::VariablesResponse response;
			auto it = var_refs.find(request.variablesReference);
			if (it == var_refs.end()) {
				return dap::Error("Unknown variablesReference '%d'", int(request.variablesReference));
			}

            // currently only dicts are stored in var_refs
			PyObject* dict = it->second;
            PyObject* key, * value;
            int pos = 0;

            while (PyDict_Next(dict, &pos, &key, &value)) {
                PyObject* keyStr = PyObject_Str(key);
                PyObject* valueStr = PyObject_Str(value);
                dap::Variable var;

                var.name = PyString_AsString(keyStr);
                var.value = PyString_AsString(valueStr);
                Py_DECREF(valueStr);
                Py_DECREF(keyStr);

				if (PyInt_Check(value)) {
                    var.type = "int";
				}
				else if (PyFloat_Check(value)) {
                    var.type = "float";
				}
				else if (PyString_Check(value)) {
                    var.type = "string";
				}
				else if (PyBool_Check(value)) {
                    var.type = "bool";
				}
				else if (PyList_Check(value)) {
                    var.type = "list";
				}
				else if (PyDict_Check(value)) {
                    var.type = "dict";
                    var.variablesReference = pyObjectHash(key);
					var_refs[var.variablesReference] = value;
				}
				else {
                    var.type = "object";
				}

                response.variables.push_back(var);
            }
        
            return response;
        });

        session->registerHandler([&](const dap::PauseRequest& request) -> dap::ResponseOrError<dap::PauseResponse> {
            if (thread_id != request.threadId) {
                return dap::Error("Invalid threadId '%d'", int(thread_id));
            }

            std::unique_lock lock(mutex);
			if (stack.empty()) {
                stopOnEntry = true;
            }
            else {
                auto& [frame, line] = stack[curindex];
                set_break(PyString_AS_STRING(frame->f_code->co_filename), line, true, "");

                if (state != Status::Stopped) {
                    statecv.wait(lock, [&] { return state == Status::Stopped; });
                }

                set_continue();
                state = Status::Running;
                statecv.notify_all();
            }

            return dap::PauseResponse();
        });

        session->registerHandler([&](const dap::ContinueRequest&) -> dap::ResponseOrError<dap::ContinueResponse> {
            std::unique_lock lock(mutex);
            if (state != Status::Stopped) {
                statecv.wait(lock, [&] { return state == Status::Stopped; });
            }

            set_continue();
            state = Status::Running;
			statecv.notify_all();
            return dap::ContinueResponse();
        });

        session->registerHandler([&](const dap::NextRequest&) {
            std::unique_lock lock(mutex);
            if (state != Status::Stopped) {
                statecv.wait(lock, [&] { return state == Status::Stopped; });
            }

            set_next(curframe);
			state = Status::Running;
            statecv.notify_all();
            return dap::NextResponse();
        });

        session->registerHandler([&](const dap::StepInRequest&) {
            std::unique_lock lock(mutex);
            if (state != Status::Stopped) {
                statecv.wait(lock, [&] { return state == Status::Stopped; });
            }
            
            set_step();
            state = Status::Running;
            statecv.notify_all();
            return dap::StepInResponse();
        });

		session->registerHandler([&](const dap::StepOutRequest&) {
            std::unique_lock lock(mutex);
            if (state != Status::Stopped) {
                statecv.wait(lock, [&] { return state == Status::Stopped; });
            }
                
            set_return(curframe);
            state = Status::Running;
            statecv.notify_all();
			return dap::StepOutResponse();
		});

        session->registerHandler([&](const dap::DisconnectRequest&) {
            terminate.fire();
            return dap::DisconnectResponse{};
        });

        session->registerHandler([&](const dap::StackTraceRequest& request) -> dap::ResponseOrError<dap::StackTraceResponse> {
            if (request.threadId != thread_id) {
                return dap::Error("Unknown threadId '%d'", int(request.threadId));
            }

            dap::StackTraceResponse response;
            std::unique_lock lock(mutex);
            std::size_t id = 0;
            for (const auto& s : stack) {
                auto pyFrame = s.first;
                assert(pyFrame->f_code && "f_code is never NULL");

                dap::StackFrame frame;
                frame.id = id;
                frame.line = s.second;
                frame.column = 1;
                frame.name = id == 0 ? "<main>" : PyString_AsString(pyFrame->f_code->co_name);

                auto filename = canonic(PyString_AsString(pyFrame->f_code->co_filename));
				dap::Source source;
				source.name = filename;
				if (filename.starts_with("<") && filename.ends_with(">")) {
                    // can only resolve by frame, not by filename
                    source.sourceReference = id;
                    source_refs.emplace(id, pyFrame);
                }
                else if (filename.contains(".zip")) {
                    source.sourceReference = id;
					source_refs.emplace(id, filename);
                }
                else {
                    // filename contains full path
                    source.path = filename;
                }
               
                frame.source = source;
                response.stackFrames.push_back(frame);
                id++;
			}

			std::ranges::reverse(response.stackFrames);
            
            return response;
        });

        session->registerHandler([&](const dap::SourceRequest& request) -> dap::ResponseOrError<dap::SourceResponse> {
            if (request.sourceReference == 0 && !request.source.has_value()) {
				return dap::Error("Invalid SourceRequest");
			}

			std::unique_lock lock(mutex);
			auto it = source_refs.find(request.sourceReference);
			if (it == source_refs.end()) {
				return dap::Error("Unknown source reference '%d'", int(request.sourceReference));
			}

			auto visitor = [&](auto&& arg) -> std::string {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, PyFrameObject*>) {
					return dis(arg->f_code);
				}
				else if constexpr (std::is_same_v<T, std::string>) {
					auto filename = arg;
					auto it = zipcache.find(filename);
					if (it != zipcache.end()) {
						return it->second;
					}
					else {
                        try {
                            if (filename.contains(".zip")) {
                                auto path = filename.substr(0, filename.find(".zip") + 4);
                                libzippp::ZipArchive zf(path);
                                zf.open(libzippp::ZipArchive::ReadOnly);
                                libzippp::ZipEntry entry = zf.getEntry(filename.substr(filename.find(".zip") + 5));
                                auto it = zipcache.emplace(filename, entry.readAsText());
                                return it.first->second;
                            }
                            return filename;
                        }
                        catch (const std::exception& e) {
                            log(std::format("err: {}", e.what()));
                            return std::format("err: {}", e.what());
                        }
					}
				}
			};

            dap::SourceResponse response;
			response.content = std::visit(visitor, it->second);
            return response;
        });

        session->registerHandler([&](const dap::SetExceptionBreakpointsRequest& request) {
            // 'never', 'always', 'unhandled', 'userUnhandled'
            //request.exceptionOptions.value().begin()->breakMode
            return dap::SetExceptionBreakpointsResponse{};
        });

        session->registerHandler([&](const dap::SetBreakpointsRequest& request) {
            dap::SetBreakpointsResponse response;

            std::unique_lock lock(mutex);
            if (request.source.path.has_value()) {
                auto path = request.source.path.value();
                std::transform(path.begin(), path.end(), path.begin(), [](auto c) { return std::tolower(c); });

                auto& fileBreaks = breaks[path];
				fileBreaks.clear();

                const auto& breakpoints = request.breakpoints.value({});
				response.breakpoints.resize(breakpoints.size());

                std::size_t i = 0;
                for (const auto& bp : breakpoints) {
                    fileBreaks[bp.line].push_back(Breakpoint(path, bp.line, false, bp.condition.value("")));
                    response.breakpoints[i++].verified = true;
                }
            }

            return response;
        });

        terminate.wait();
        std::unique_lock lock(mutex);
        
        auto it = std::find(sessions.begin(), sessions.end(), session);
        sessions.erase(it);

        std::println("Server closing connection");
        if (sessions.empty()) {
            state = Status::Running;
            statecv.notify_all();
        }
    };

    auto onError = [&](const char* msg) {
        std::println("Server error: {}", msg);
    };

    // wait on entry
    set_step();
    server->start(port, onClientConnected, onError);
    std::println("server waiting on {}", port);
}

void debugger::stop()
{
    server->stop();
}

void debugger::user_entry(PyFrameObject* frame)
{
    if (stopOnEntry) {
        return;
    }
    else if (sessions.empty()) {
        std::unique_lock lock(mutex);
        statecv.wait(lock, [&] { return !sessions.empty(); });
    }

    dap::StoppedEvent event;
    event.reason = "entry";
    event.threadId = thread_id;
    send(event);

	interaction(frame, nullptr);
}

void debugger::user_line(PyFrameObject* frame)
{
    dap::StoppedEvent event;
    event.threadId = thread_id;
    event.reason = "step";
	send(event);

    interaction(frame, nullptr);
}

void debugger::user_call(PyFrameObject* frame)
{
    if (stop_here(frame)) {
        dap::StoppedEvent event;
        event.threadId = thread_id;
        event.reason = "step";
        send(event);

        interaction(frame, nullptr);
    }
}

void debugger::user_return(PyFrameObject* frame, PyObject* arg)
{
    if (frame->f_locals) {
        PyDict_SetItemString(frame->f_locals, "__return__", arg);
    }

    dap::StoppedEvent event;
	event.threadId = thread_id;
    event.reason = "step";
    send(event);
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
        dap::StoppedEvent event;
        event.threadId = thread_id;
        event.reason = "exception";
        event.text = std::format("{}: {}", PyString_AsString(typeStr), PyString_AsString(valueRepr));
        send(event);
    }

    Py_XDECREF(typeStr);
    Py_XDECREF(valueRepr);

    interaction(frame, traceback);
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

void debugger::interaction(PyFrameObject* frame, PyObject* traceback)
{
	std::unique_lock lock(mutex);
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