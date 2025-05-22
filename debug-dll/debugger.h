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

#include <io.h>
#include <dap/network.h>
#include <dap/session.h>
#include <mutex>
#include <variant>
#include <map>
#include <chrono>

class Event {
public:
	// wait() blocks until the event is fired.
	void wait();
	template< class Rep, class Period >
	void wait_for(const std::chrono::duration<Rep, Period>& time)
	{
		std::unique_lock lock(mutex);
		cv.wait_for(lock, time, [&] { return fired; });
	}

	// fire() sets signals the event, and unblocks any calls to wait().
	void fire();

	operator bool() const noexcept { return fired; }

private:
	std::mutex mutex;
	std::condition_variable cv;
	bool fired = false;
};

class debugger : public bdb {
	std::condition_variable statecv;
	std::size_t thread_id;
	bool stopOnEntry;

	enum class Status {
		Running,
		Stopped
	} state = Status::Running;

	struct sess {
		std::shared_ptr<dap::ReaderWriter> socket;
		std::shared_ptr<dap::Session> session;
		Event* terminate;
	};

	std::deque<std::pair<PyFrameObject*, std::size_t>> stack;
	std::size_t curindex = 0;
	PyFrameObject* curframe = nullptr;

	std::unordered_map<dap::integer, PyObject*> var_refs;
	dap::integer last_source_id;
	std::unordered_map<dap::integer, std::string> source_cache;
	std::unordered_map<dap::integer, std::variant<std::string, PyFrameObject*>> source_refs;
	std::unique_ptr<dap::net::Server> server;
	std::vector<sess> sessions;
	std::unordered_map<std::string, std::string> zipcache;
	std::map<std::string, PyCFunction> hostModule;

public:
	debugger(bool stopOnEntry);

	void setHostModule(const decltype(hostModule)& _hostModule);

	void start(int port = 19021);
	void stop();

	void log(const std::string& msg);
	template<typename... Args>
	void log(const char* fmt, Args&&... args)
	{
		log(std::vformat(fmt, std::make_format_args(args...)));
	}
	
private:
	virtual void user_entry(std::unique_lock<std::mutex>& lock, PyFrameObject* frame) override;
	virtual void user_call(std::unique_lock<std::mutex>& lock, PyFrameObject* frame) override;
	virtual void user_line(std::unique_lock<std::mutex>& lock, PyFrameObject* frame) override;
	virtual void user_return(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg) override;
	virtual void user_exception(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* arg) override;
	virtual void on_breakpoint_error(Breakpoint& bp, const std::string& message) override;
	virtual void do_clear(Breakpoint& bp) override;

	void send_mod_path();

	void interaction(std::unique_lock<std::mutex>& lock, PyFrameObject* frame, PyObject* traceback);
	void setup(PyFrameObject* frame, PyObject* traceback);
	void forget();

	template <typename T>
	void send(const T& event)
	{
		for (auto it = sessions.begin(); it != sessions.end();) {
			if (!it->socket->isOpen()) {
				it->terminate->fire();
				it = sessions.erase(it);
			}
			else {
				it->session->send(event);
				++it;
			}
		}
	}
};