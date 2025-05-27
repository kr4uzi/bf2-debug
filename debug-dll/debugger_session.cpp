#include "debugger_session.h"
#include "debugger.h"
#include <string>
#include <iostream>
#include <print>
#include <ranges>
#include <algorithm>
#include <chrono>
#include "dis.h"
#include <libzippp/libzippp.h>

std::hash<PyObject*> pyObjectHash{};
std::hash<std::string> filenameHash{};
using namespace nlohmann;
debugger_session::debugger_session(debugger& debugger, asio::ip::tcp::socket socket)
	: _debugger(debugger), _socket(std::move(socket))
{

}

std::uint32_t get_packet_length(asio::streambuf& buffer)
{
	auto stream = std::istream{ &buffer };
	std::string header;
	std::uint32_t length = 0;
	while (std::getline(stream, header)) {
		if (header.empty() || header == "\r") {
			break;
		}
		if (header.starts_with("Content-Length:")) {
			auto pos = header.find(':');
			if (pos != std::string::npos) {
				auto end = std::string::size_type{};
				length = std::stoul(header.substr(pos + 1), &end);
				if (end == header.length() - std::string_view{ "Content-Length:" }.length()) {
					return 0;
				}
			}
		}
	}

	return length;
}

asio::awaitable<void> debugger_session::run()
{
	try {
		auto buffer = asio::streambuf{};
		while (_socket.is_open()) {
			const auto& [error, recvLength] = co_await asio::async_read_until(_socket, buffer, "\r\n\r\n", asio::as_tuple(asio::use_awaitable));
			if (error) {
				break;
			}

			auto pkgLength = get_packet_length(buffer);
			if (pkgLength == 0) {
				std::println(stderr, "Received empty packet, closing session.");
				break;
			}

			if (buffer.size() < pkgLength) {
				co_await asio::async_read(_socket, buffer, asio::transfer_exactly(pkgLength - buffer.size()), asio::use_awaitable);
			}

			const char* data = asio::buffer_cast<const char*>(buffer.data());
			json packet = json::parse(data, data + pkgLength);
			buffer.consume(pkgLength);

			const auto& type = packet["type"];
			if (type == "request") {
				using namespace nlohmann::literals;
				const auto& command = packet["command"];
				if (command == "initialize") {
					co_await handle_initialize(packet);
				}
				else if (command == "attach") {
					co_await handle_attach(packet);
				}
				else if (command == "configurationDone") {
					co_await handle_configurationDone(packet);
				}
				else if (command == "threads") {
					// TODO: the enable_trace should not be called on the debugger itself, instead here (and if wait_on_entry=true)
					// in addition, the PyThread_start_new_thread and PyThread_exit_thread should be overwritten so that we can send an thread event
					// or should i overwrite PyThreadState_New? not sure. if threading is enabled a new threadstate will start a new thread automatically,
					// but PyEval_SetTrace will only register on the current thread state and if a python code creates new threads, does this mean that
					// on those, the Trace wont work?
					// on pause do we need PyGILState_Ensure?
					
					
					for (auto interpreter = PyInterpreterState_Head(); interpreter; interpreter = PyInterpreterState_Next(interpreter)) {
						std::println("interpreter {}", (void*)interpreter);
						for (auto thread = PyInterpreterState_ThreadHead(interpreter); thread; thread = PyThreadState_Next(thread)) {
							std::println("thread {}", (void*)thread);
							// enable_trace
						}
					}
					co_await handle_threads(packet);
				}
				else if (command == "stackTrace") {
					co_await handle_stackTrace(packet);
				}
				else if (command == "scopes") {
					co_await handle_scopes(packet);
				}
				else if (command == "variables") {
					co_await handle_variables(packet);
				}
				else if (command == "source") {
					co_await handle_source(packet);
				}
				else if (command == "setExceptionBreakpoints") {
					co_await handle_setExceptionBreakpoints(packet);
				}
				else if (command == "setBreakpoints") {
					co_await handle_setBreakpoints(packet);
				}
				else if (command == "pause") {
					co_await handle_pause(packet);
				}
				else if (command == "continue") {
					co_await handle_continue(packet);
				}
				else if (command == "next") {
					co_await handle_next(packet);
				}
				else if (command == "stepIn") {
					co_await handle_stepIn(packet);
				}
				else if (command == "stepOut") {
					co_await handle_stepOut(packet);
				}
				else if (command == "disconnect") {
					co_await handle_disconnect(packet);
				}
				else {
					std::println(stderr, "[session][error] Unknown request: {}", packet.dump(4));
				}
			}
		}
	}
	catch (std::exception& e) {
		std::println(stderr, "[session][error] {}", e.what());
	}
}

void debugger_session::send_modpath(const std::string& modPath)
{
	send_sync({
			{ "type", "event" },
			{ "event", "bf2py" },
			{ "body", {
				{ "type", "modpath" },
				{ "data",  modPath }
			}}
		});
}

void debugger_session::send_entry(std::uint32_t threadId)
{
	send_sync({
			{ "type", "event" },
			{ "event", "stopped" },
			{ "body", {
				{ "reason", "entry" },
				{ "threadId",  threadId }
			}}
		});
}

void debugger_session::send_step(std::uint32_t threadId)
{
	send_sync({
			{ "type", "event" },
			{ "event", "stopped" },
			{ "body", {
				{ "reason", "step" },
				{ "threadId",  threadId }
			}}
		});
}

void debugger_session::send_exception(std::uint32_t threadId, const std::string& text)
{
	send_sync({
			{ "type", "event" },
			{ "event", "stopped" },
			{ "body", {
				{ "reason", "exception" },
				{ "threadId",  threadId },
				{ "text",  text }
			}}
		});
}

void debugger_session::send_output(const std::string& output)
{
	send_sync({
			{ "type", "event" },
			{ "event", "output" },
			{ "body", {
				{ "category", "console" },
				{ "output", output }
			}}
		});
}

void debugger_session::send_sync(const json& data)
{
	auto dataBytes = data.dump();
	_socket.send(asio::buffer(std::format("Content-Length: {}\r\n\r\n{}", dataBytes.size(), dataBytes)));
}

asio::awaitable<void> debugger_session::async_send(const json& data)
{
	auto dataBytes = data.dump();
	co_await _socket.async_send(asio::buffer(std::format("Content-Length: {}\r\n\r\n{}", dataBytes.size(), dataBytes)), asio::use_awaitable);
}

asio::awaitable<void> debugger_session::async_send_response(const json& request, const json& body, bool success)
{
	const auto response = json{
		{ "type", "response" },
		{ "request_seq", request["seq"] },
		{ "success", success },
		{ "command", request["command"] },
		{ "body", body }
	};

	co_await async_send(response);
}

asio::awaitable<void> debugger_session::async_send_event(const std::string& event, const json& body)
{
	auto data = json{
		{ "type", "event" },
		{ "event", event }
	};

	if (!body.empty()) {
		data["body"] = body;
	}

	co_await async_send(data);
}

void debugger_session::forget(std::uint32_t threadId)
{
	_var_refs.clear();
	_source_refs.clear();
}

asio::awaitable<void> debugger_session::handle_initialize(const json& packet)
{
	co_await async_send_response(packet, {
			{ "supportsConfigurationDoneRequest", true },
			{ "exceptionBreakpointFilters", json::array({
					{ { "filter", "never" }, { "label", "Never" } },
					{ { "filter", "always" }, { "label", "Always" } },
					{ { "filter", "unhandled" }, { "label", "Unhandled" } }
				}) }
		});

	co_await async_send_event("initialized", {});
}

asio::awaitable<void> debugger_session::handle_attach(const json& packet)
{
	co_await async_send_response(packet, {});
}

asio::awaitable<void> debugger_session::handle_configurationDone(const json& packet)
{
	_initialized = true;
	co_await async_send_response(packet, {});
}

asio::awaitable<void> debugger_session::handle_threads(const json& packet)
{
	co_await async_send_response(packet, {
			{ "threads", json::array({
					{ { "id", _debugger.thread_id() }, { "name", "bf2" } } }
				)}
		});
}

asio::awaitable<void> debugger_session::handle_stackTrace(const json& packet)
{
	const auto threadId = packet["arguments"]["threadId"].get<std::uint32_t>();
	if (threadId != _debugger.thread_id()) {
		co_await async_send_response(packet, {
				{ "error", std::format("Invalid threadId '{}'", threadId) }
			}, false);
		co_return;
	}

	auto stackFrames = json::array();
	std::uint32_t frameId = 0;

	for (const auto& s : std::views::reverse(_debugger.stack())) {
		auto frame = s.first;
		assert(frame->f_code && "f_code is never NULL");

		auto source = json::object();
		auto filename = _debugger.canonic(PyString_AsString(frame->f_code->co_filename));
		if (filename.starts_with("<") && filename.ends_with(">")) {
			auto sourceRef = _last_source_id++;
			source["sourceReference"] = sourceRef;
			_source_refs.emplace(sourceRef, frame);
		}
		else if (filename.contains(".zip")) {
			auto sourceRef = _last_source_id++;
			source["sourceReference"] = sourceRef;
			_source_refs.emplace(sourceRef, filename);
		}
		else {
			source["path"] = filename;
		}

		stackFrames.push_back({
				{ "id", frameId++ },
				{ "name", PyString_AsString(frame->f_code->co_name) },
				{ "line", s.second },
				{ "column", 1 },
				{ "source", source }
			});
	}

	co_await async_send_response(packet, {
			{ "stackFrames", stackFrames }
		});
}

asio::awaitable<void> debugger_session::handle_scopes(const json& packet)
{
	const auto& stack = _debugger.stack();
	const auto frameId = packet["arguments"]["frameId"].get<std::uint32_t>();
	if (frameId > stack.size()) {
		co_await async_send_response(packet, {
			{ "error", std::format("Invalid frameId '{}'", frameId) }
			}, false);
		co_return;
	}

	auto scopes = json::array();
	const auto& frame = stack[frameId].first;
	if (frame->f_locals) {
		const auto refId = pyObjectHash(frame->f_locals);
		scopes.push_back({
				{ "name", "Locals" },
				{ "presentationHint", "locals" },
				{ "variablesReference", refId }
			});
		_var_refs[refId] = frame->f_locals;
	}

	if (frame->f_globals && frame->f_globals != frame->f_locals) {
		const auto refId = pyObjectHash(frame->f_globals);
		scopes.push_back({
				{ "name", "Globals" },
				{ "variablesReference", refId }
			});
		_var_refs[refId] = frame->f_globals;
	}

	co_await async_send_response(packet, {
			{ "scopes", scopes }
		});
}

asio::awaitable<void> debugger_session::handle_variables(const json& packet)
{
	const auto varId = packet["arguments"]["variablesReference"].get<std::uint32_t>();
	auto it = _var_refs.find(varId);
	if (it == _var_refs.end()) {
		co_await async_send_response(packet, {
				{ "error", std::format("Invalid variablesReference '{}'", varId) }
			}, false);
		co_return;
	}

	// currently only dicts are stored in var_refs
	PyObject* dict = it->second;
	PyObject* key, * value;
	int pos = 0;

	auto variables = json::array();
	while (PyDict_Next(dict, &pos, &key, &value)) {
		std::string type;
		std::uint32_t varId = 0;
		if (PyInt_Check(value)) {
			type = "int";
		}
		else if (PyFloat_Check(value)) {
			type = "float";
		}
		else if (PyString_Check(value)) {
			type = "string";
		}
		else if (PyBool_Check(value)) {
			type = "bool";
		}
		else if (PyList_Check(value)) {
			type = "list";
		}
		else if (PyDict_Check(value)) {
			type = "dict";
			varId = pyObjectHash(key);
			_var_refs[varId] = value;
		}
		else {
			type = "object";
		}

		PyObject* keyStr = PyObject_Str(key);
		PyObject* valueStr = PyObject_Str(value);
		auto variable = json::object({
			{ "name", PyString_AsString(keyStr) },
			{ "type", type },
			{ "value", PyString_AsString(valueStr) },
			{ "variablesReference", varId }
			});

		Py_DECREF(valueStr);
		Py_DECREF(keyStr);

		variables.push_back(variable);
	}

	co_await async_send_response(packet, {
		{ "variables", variables }
		});
}

asio::awaitable<void> debugger_session::handle_source(const json& packet)
{
	const auto sourceRef = packet["arguments"]["sourceReference"].get<std::uint32_t>();
	const auto cit = _source_cache.find(sourceRef);
	if (cit != _source_cache.end()) {
		co_await async_send_response(packet, {
			{ "content", cit->second }
			});
		co_return;
	}

	auto rit = _source_refs.find(sourceRef);
	if (rit == _source_refs.end()) {
		co_await async_send_response(packet, {
			{ "error", std::format("Invalid source reference '{}'", sourceRef) }
			}, false);
		co_return;
	}

	auto visitor = [&](auto&& arg) -> std::string {
		using T = std::decay_t<decltype(arg)>;
		if constexpr (std::is_same_v<T, PyFrameObject*>) {
			return dis(arg->f_code);
		}
		else if constexpr (std::is_same_v<T, std::string>) {
			auto filename = arg;
			auto it = _zipcache.find(filename);
			if (it != _zipcache.end()) {
				return it->second;
			}
			else {
				try {
					if (filename.contains(".zip")) {
						auto path = filename.substr(0, filename.find(".zip") + 4);
						libzippp::ZipArchive zf(path);
						zf.open(libzippp::ZipArchive::ReadOnly);
						libzippp::ZipEntry entry = zf.getEntry(filename.substr(filename.find(".zip") + 5));
						auto it = _zipcache.emplace(filename, entry.readAsText());
						return it.first->second;
					}
					return filename;
				}
				catch (const std::exception& e) {
					return std::format("err: {}", e.what());
				}
			}
		}
		};

	const auto content = std::visit(visitor, rit->second);
	_source_cache.emplace(sourceRef, content);
	co_await async_send_response(packet, {
		{ "content", content }
		});
}

asio::awaitable<void> debugger_session::handle_setBreakpoints(const json& packet)
{
	auto path = packet["arguments"]["source"].value("path", std::string());
	auto response = json::object();
	if (!path.empty()) {
		std::ranges::transform(path, path.begin(), [](auto c) { return std::tolower(c); });

		auto& fileBreaks = _debugger.breaks()[path];
		fileBreaks.clear();

		auto validatedBreaks = json::array();
		for (const auto& bp : packet["arguments"].value("breakpoints", json::array())) {
			const auto line = bp["line"].get<std::uint32_t>();
			fileBreaks[line].push_back(
				Breakpoint(path, line, false, bp.value("condition", ""))
			);
			validatedBreaks.push_back({
				{ "verified", true }
				});
		}
		response["breakpoints"] = validatedBreaks;
	}

	co_await async_send_response(packet, response);
}

asio::awaitable<void> debugger_session::handle_setExceptionBreakpoints(const json& packet)
{
	auto exmode = bdb::exception_mode::NEVER;
	for (const auto& filter : packet["arguments"]["filters"]) {
		if (filter == "never") {
			exmode = bdb::exception_mode::NEVER;
		}
		else if (filter == "always") {
			exmode = bdb::exception_mode::ALL_EXCEPTIONS;
		}
		else if (filter == "unhandled") {
			exmode = bdb::exception_mode::UNHANDLED_EXCEPTION;
		}
	}

	_debugger.set_exception_mode(exmode);

	co_await async_send_response(packet, {});
}

asio::awaitable<void> debugger_session::handle_pause(const json& packet)
{
	_debugger.pause();

	auto timer = asio::steady_timer{co_await asio::this_coro::executor};
	timer.expires_after(std::chrono::seconds(10));
		co_await timer.async_wait(asio::use_awaitable);

	co_await async_send_response(packet, {});
}

asio::awaitable<void> debugger_session::handle_continue(const json& packet)
{
	_debugger.set_continue();
	_debugger.state(debugger::Status::Running);
	co_await async_send_response(packet, {});
}

asio::awaitable<void> debugger_session::handle_next(const json& packet)
{
	_debugger.set_next(_debugger.current_frame());
	_debugger.state(debugger::Status::Running);
	co_await async_send_response(packet, {});
}

asio::awaitable<void> debugger_session::handle_stepIn(const json& packet)
{
	_debugger.set_step();
	_debugger.state(debugger::Status::Running);
	co_await async_send_response(packet, {});
}

asio::awaitable<void> debugger_session::handle_stepOut(const json& packet)
{
	_debugger.set_return(_debugger.current_frame());
	_debugger.state(debugger::Status::Running);
	co_await async_send_response(packet, {});
}

asio::awaitable<void> debugger_session::handle_disconnect(const json& packet)
{
	co_await async_send_response(packet, {});
	_socket.close();
}
