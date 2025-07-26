#pragma once
#include "asio.h"
#include "bdb.h"
#include "debugger_session.h"
#include <cstddef>
#include <map>
#include <deque>
#include <optional>
#include <thread>

namespace bf2py {
	class debugger : public bdb {
	public:
		enum class Status {
			Running,
			Stopped
		};

		using thread_id_t = decltype(PyThreadState::thread_id);

	private:
		asio::io_context _ctx;
		asio::ip::port_type _port = 5678;
		bool _wait_for_connection = true;
		std::jthread _io_runner;

		std::optional<debugger_session> _session;

		Status _state = Status::Running;

		std::deque<std::pair<PyFrameObject*, std::size_t>> _stack;
		std::size_t _curindex = 0;
		PyFrameObject* _curframe = nullptr;
		thread_id_t _curthread = -1;

		std::map<std::string, PyCFunction> _hostModule;

	public:
		void setHostModule(const decltype(_hostModule)& _hostModule);

		void start();
		void stop();

		void log(const std::u8string& msg);
		template<typename... Args>
		void log(const char8_t* fmt, Args&&... args)
		{
			log(std::vformat(fmt, std::make_format_args(args...)));
		}
		void log(const std::string& msg);
		template<typename... Args>
		void log(const char* fmt, Args&&... args)
		{
			log(std::vformat(fmt, std::make_format_args(args...)));
		}

		const auto& stack() const { return _stack; }
		auto& breaks() { return _breaks; }
		const auto& current_frame() const { return _curframe; }
		const auto& current_thread() const { return _curthread; }
		
		auto state() const { return _state; }
		void state(decltype(_state) state) { _state = state; }
		
		auto wait_for_connection() const { return _wait_for_connection; }
		void wait_for_connection(bool wait) { _wait_for_connection = wait; }

		auto port() const { return _port; }
		void port(decltype(_port) port) { _port = port; }

	private:
		asio::awaitable<void> run();
		void start_io_runner();

		virtual int trace_dispatch(PyFrameObject* frame, int event, PyObject* arg);
		virtual void user_entry(PyFrameObject* frame) override;
		virtual void user_call(PyFrameObject* frame) override;
		virtual void user_line(PyFrameObject* frame) override;
		virtual void user_return(PyFrameObject* frame, PyObject* returnValue) override;
		virtual void user_exception(PyFrameObject* frame, PyObject* excInfo) override;
		virtual void on_breakpoint_error(Breakpoint& bp, const std::string& message) override;
		virtual void do_clear(Breakpoint& bp) override;

		void interaction(PyFrameObject* frame, PyObject* traceback);
		void setup(PyFrameObject* frame, PyObject* traceback);
		void forget();

		void run_until(auto fn)
		{
			while (!fn()) {
				_ctx.run_one();
			}
		}
	};
}