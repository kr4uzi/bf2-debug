#include "debugger.h"
#include "dis.h"
#include <libzippp/libzippp.h>
#include <ranges>
#include <algorithm>
#include <dap/protocol.h>
#include <dap/session.h>
#include <filesystem>

namespace dap {
    struct BF2DAPEvent : public Event {
        dap::string type;
        dap::string data;
    };

    DAP_DECLARE_STRUCT_TYPEINFO(BF2DAPEvent);
    DAP_IMPLEMENT_STRUCT_TYPEINFO(BF2DAPEvent,
        "bf2py",
        DAP_FIELD(type, "type"),
        DAP_FIELD(data, "data"));
}

void debugger::send_mod_path()
{
    // note: the bf2 host module is initialized *after* sys.path is set using PyRun_SimpleString
    // this means that if started with stopOnEntry cannot yet resolve the current mod path
    auto it = hostModule.find("sgl_getModDirectory");
    if (it != hostModule.end()) {
        // decompiled bf2 server executable showed that arguments are not used and a string is always returned
        auto modDir = it->second(nullptr, nullptr);
        if (modDir) {
            dap::BF2DAPEvent evt;
            evt.type = "modpath";
            evt.data = std::format("{};{}", std::filesystem::current_path().string(), PyString_AS_STRING(modDir));
            send(evt);
        }
    }
}

void debugger::start(int port)
{
    auto onClientConnected = [this](const std::shared_ptr<dap::ReaderWriter>& socket) {
        std::hash<PyObject*> pyObjectHash{};
        std::hash<std::string> filenameHash{};

        Event terminate, initialized;
        std::shared_ptr session = dap::Session::create();
        session->bind(socket);

        session->onError([&](const char* msg) {
            std::println("Session error: {}", msg);
            terminate.fire();
        });

        session->registerHandler([&](const dap::InitializeRequest&) {
            dap::InitializeResponse response;
            response.supportsConfigurationDoneRequest = true;
            // 'never', 'always', 'unhandled', 'userUnhandled'
            dap::array<dap::ExceptionBreakpointsFilter> exFilters;
            exFilters.push_back(dap::ExceptionBreakpointsFilter{
                .filter = "never",
                .label = "Never"
            });
            exFilters.push_back(dap::ExceptionBreakpointsFilter{
                .filter = "always",
                .label = "Alawys"
            });
            exFilters.push_back(dap::ExceptionBreakpointsFilter{
                .filter = "unhandled",
                .label = "Unhandled"
            });
            response.exceptionBreakpointFilters = exFilters;
            return response;
        });

        session->registerSentHandler([&](const dap::ResponseOrError<dap::InitializeResponse>&) {
            session->send(dap::InitializedEvent());
            initialized.fire();
        });

        session->registerHandler([&](const dap::AttachRequest&) {
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
                // i think if f_locals are null if using PyEval_CallObject
                // PyFrame_FastToLocals(pyFrame);
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
            pause();
            stopOnEntry = true; // not 100% correct: if request still running, pause will additionally cause a break on next mframe
            return dap::PauseResponse();
        });

        session->registerHandler([&](const dap::ContinueRequest&) -> dap::ResponseOrError<dap::ContinueResponse> {
            std::unique_lock lock(mutex);
            while (socket->isOpen() && state != Status::Stopped) {
                statecv.wait_for(lock, std::chrono::milliseconds(100), [&] { return state == Status::Stopped; });
            }

            set_continue();
            return dap::ContinueResponse();
        });

        session->registerSentHandler([&](const dap::ResponseOrError<dap::ContinueResponse>&) {
            std::unique_lock lock(mutex);
            state = Status::Running;
            statecv.notify_all();
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
            dap::integer frameId;
            for (const auto& s : stack) {
                auto pyFrame = s.first;
                assert(pyFrame->f_code && "f_code is never NULL");

                dap::StackFrame frame;
                frame.id = frameId++;
                frame.line = s.second;
                frame.column = 1;
                frame.name = PyString_AsString(pyFrame->f_code->co_name);

                auto filename = canonic(PyString_AsString(pyFrame->f_code->co_filename));
                dap::Source source;
                source.name = filename;
                if (filename.starts_with("<") && filename.ends_with(">")) {
                    // can only resolve by frame, not by filename
                    source.sourceReference = last_source_id++;
                    source_refs.emplace(*source.sourceReference, pyFrame);
                }
                else if (filename.contains(".zip")) {
                    source.sourceReference = last_source_id++;
                    source_refs.emplace(*source.sourceReference, filename);
                }
                else {
                    // filename contains full path
                    source.path = filename;
                }

                frame.source = source;
                response.stackFrames.push_back(frame);
            }

            std::ranges::reverse(response.stackFrames);

            return response;
        });

        session->registerHandler([&](const dap::SourceRequest& request) -> dap::ResponseOrError<dap::SourceResponse> {
            if (request.sourceReference == 0 && !request.source.has_value()) {
                return dap::Error("Invalid SourceRequest");
            }

            std::unique_lock lock(mutex);
            auto cit = source_cache.find(request.sourceReference);
            if (cit != source_cache.end()) {
                dap::SourceResponse response;
                response.content = cit->second;
                return response;
            }

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
            source_cache.emplace(request.sourceReference, response.content);
            return response;
        });

        session->registerHandler([&](const dap::SetExceptionBreakpointsRequest& request) {
            // 'never', 'always', 'unhandled', 'userUnhandled'
            auto exmode = bdb::exception_mode::NEVER;
            if (request.exceptionOptions) {
                for (const auto& option : *request.exceptionOptions) {
                    if (option.breakMode == "never") {
                        exmode = bdb::exception_mode::NEVER;
                    }
                    else if (option.breakMode == "always") {
                        exmode = bdb::exception_mode::ALL_EXCEPTIONS;
                    }
                    else if (option.breakMode == "unhandled") {
                        exmode = bdb::exception_mode::UNHANDLED_EXCEPTION;
                    }
                }

                std::unique_lock lock(mutex);
                set_exception_mode(exmode);
            }
            return dap::SetExceptionBreakpointsResponse{};
        });

        session->registerHandler([&](const dap::SetBreakpointsRequest& request) {
            dap::SetBreakpointsResponse response;

            std::unique_lock lock(mutex);
            if (request.source.path.has_value()) {
                auto path = request.source.path.value();
                std::ranges::transform(path, path.begin(), [](auto c) { return std::tolower(c); });

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

        initialized.wait_for(std::chrono::seconds(500));
        if (!initialized) {
            std::println("Server closing connection");
            socket->close();
            return;
        }

        {
            std::unique_lock lock(mutex);
            sessions.emplace_back(socket, session, &terminate);
            send_mod_path();
            statecv.notify_all();
        }
        terminate.wait();
        {
            std::unique_lock lock(mutex);
            auto it = std::find_if(sessions.begin(), sessions.end(), [&](const auto& s) { return s.socket == socket; });
            sessions.erase(it);
            socket->close();

            std::println("Server closing connection");
            if (sessions.empty()) {
                state = Status::Running;
                statecv.notify_all();
            }
        }
    };

    if (stopOnEntry) {
        //set_step();
    }

    server->start(port, onClientConnected, [&](const char* msg) {
        std::println("Server error: {}", msg);
    });
    std::println("server waiting on {}", port);
}