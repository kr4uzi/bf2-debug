#pragma once
#include "asio.h"
#include "bdb.h"
#include "debugger_session.h"
#include <cstdint>
#include <map>
#include <optional>
#include <thread>
#include <variant>

class debugger : public bdb {
public:
	enum class Status {
		Running,
		Stopped
	};

private:
	asio::io_context _ctx;
	std::uint32_t _thread_id;
	bool _wait_for_connection;
	std::jthread _runner;
	std::optional<debugger_session> _session;

	Status _state = Status::Running;

	std::deque<std::pair<PyFrameObject*, std::size_t>> _stack;
	std::size_t _curindex = 0;
	PyFrameObject* _curframe = nullptr;

	std::map<std::string, PyCFunction> _hostModule;

public:
	debugger(bool stopOnEntry);

	virtual int trace_dispatch(PyFrameObject* frame, int event, PyObject* arg);

	void setHostModule(const decltype(_hostModule)& _hostModule);

	void start(asio::ip::port_type port = 19021);
	void stop();

	void log(const std::string& msg);
	template<typename... Args>
	void log(const char* fmt, Args&&... args)
	{
		log(std::vformat(fmt, std::make_format_args(args...)));
	}

	std::uint32_t thread_id() const { return _thread_id; }
	const auto& stack() const { return _stack; }
	auto& breaks() { return _breaks; }
	const auto& current_frame() const { return _curframe; }
	auto state() const { return _state; }
	void state(decltype(_state) state) { _state = state; }

private:
	asio::awaitable<void> run(asio::ip::port_type port);
	void start_io_runner();

	virtual void user_entry(PyFrameObject* frame) override;
	virtual void user_call(PyFrameObject* frame) override;
	virtual void user_line(PyFrameObject* frame) override;
	virtual void user_return(PyFrameObject* frame, PyObject* arg) override;
	virtual void user_exception(PyFrameObject* frame, PyObject* arg) override;
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