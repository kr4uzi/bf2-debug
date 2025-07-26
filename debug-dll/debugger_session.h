#pragma once
#include "asio.h"
#include "python.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <variant>

namespace bf2py {
	class debugger;
	class debugger_session
	{
		debugger& _debugger;
		asio::ip::tcp::socket _socket;
		bool _initialized = false;

		std::unordered_map<std::uint32_t, PyFrameObject*> _frame_refs;
		std::unordered_map<std::uint32_t, PyObject*> _var_refs;
		std::uint32_t _last_source_id = 1;
		std::unordered_map<std::uint32_t, std::string> _source_cache;
		std::unordered_map<std::uint32_t, std::variant<std::string, PyFrameObject*>> _source_refs;
		std::unordered_map<std::string, std::string> _zipcache;

	public:
		debugger_session(debugger& debugger, asio::ip::tcp::socket socket);

		bool initialized() const { return _initialized; }
		asio::awaitable<void> run();

		void send_sync(const nlohmann::json& data);
		asio::awaitable<void> async_send(const nlohmann::json& data);

		void send_modpath(const std::string& modPath);
		void send_entry(std::uint32_t threadId);
		void send_step(std::uint32_t threadId);
		void send_exception(std::uint32_t threadId, const std::string& text);
		void send_output(const std::string& output);
		void send_output(const std::u8string& output);

		void forget(std::uint32_t threadId);

	private:
		asio::awaitable<void> async_send_response(const nlohmann::json& request, const nlohmann::json& body, bool success = true);
		asio::awaitable<void> async_send_event(const std::string& event, const nlohmann::json& body);

		asio::awaitable<void> handle_initialize(const nlohmann::json& packet);
		asio::awaitable<void> handle_attach(const nlohmann::json& packet);
		asio::awaitable<void> handle_configurationDone(const nlohmann::json& packet);
		asio::awaitable<void> handle_threads(const nlohmann::json& packet);
		asio::awaitable<void> handle_stackTrace(const nlohmann::json& packet);
		asio::awaitable<void> handle_scopes(const nlohmann::json& packet);
		asio::awaitable<void> handle_variables(const nlohmann::json& packet);
		asio::awaitable<void> handle_source(const nlohmann::json& packet);
		asio::awaitable<void> handle_setBreakpoints(const nlohmann::json& packet);
		asio::awaitable<void> handle_setExceptionBreakpoints(const nlohmann::json& packet);
		asio::awaitable<void> handle_pause(const nlohmann::json& packet);
		asio::awaitable<void> handle_continue(const nlohmann::json& packet);
		asio::awaitable<void> handle_next(const nlohmann::json& packet);
		asio::awaitable<void> handle_stepIn(const nlohmann::json& packet);
		asio::awaitable<void> handle_stepOut(const nlohmann::json& packet);
		asio::awaitable<void> handle_disconnect(const nlohmann::json& packet);
		asio::awaitable<void> handle_evaluate(const nlohmann::json& packet);
	};
}