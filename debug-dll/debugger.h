#pragma once
#include "bdb.h"
#include <dap/types.h>
template <>
struct std::hash<dap::integer>
{
	auto operator()(const dap::integer& k) const
	{
		std::int64_t val{ k };
		return val;
	}
};

#include <dap/network.h>
#include <dap/session.h>
#include <mutex>
#include <variant>
#include <map>

class Event {
public:
	// wait() blocks until the event is fired.
	void wait();

	// fire() sets signals the event, and unblocks any calls to wait().
	void fire();

private:
	std::mutex mutex;
	std::condition_variable cv;
	bool fired = false;
};

class debugger : public bdb {
	std::recursive_mutex mutex;
	std::condition_variable_any statecv;
	std::size_t thread_id;
	bool stopOnEntry;

	enum class Status {
		Running,
		Stopped
	} state = Status::Running;

	std::deque<std::pair<PyFrameObject*, std::size_t>> stack;
	std::size_t curindex = 0;
	PyFrameObject* curframe = nullptr;

	std::unordered_map<dap::integer, PyObject*> var_refs;
	std::unordered_map<dap::integer, std::variant<std::string, PyFrameObject*>> source_refs;
	std::unique_ptr<dap::net::Server> server;
	std::vector<std::shared_ptr<dap::Session>> sessions;
	std::unordered_map<std::string, std::string> zipcache;

public:
	debugger(bool stopOnEntry);

	void start(int port = 19021);
	void stop();

	void log(const std::string& msg);
	template<typename... Args>
	void log(const char* fmt, Args&&... args)
	{
		log(std::vformat(fmt, std::make_format_args(args...)));
	}
	
private:
	virtual void user_line(PyFrameObject* frame) override;
	virtual void user_call(PyFrameObject* frame) override;
	virtual void user_return(PyFrameObject* frame, PyObject* arg) override;
	virtual void user_exception(PyFrameObject* frame, PyObject* arg) override;
	virtual void do_clear(Breakpoint& bp) override;
	virtual void user_entry(PyFrameObject* frame) override;

	void interaction(PyFrameObject* frame, PyObject* traceback);
	void setup(PyFrameObject* frame, PyObject* traceback);
	void forget();

	template <typename T>
	void send(const T& event)
	{
		std::unique_lock lock(mutex);
		for (auto& session : sessions) {
			session->send(event);
		}
	}
};