#pragma once
#ifndef _BF2PY_DEBUG_FILE_REDIRECT_H_
#define _BF2PY_DEBUG_FILE_REDIRECT_H_

#include <thread>
#include <string>
#include <functional>

namespace bf2py {
	class output_redirect
	{
		std::FILE* _out = nullptr;
		int _out_alias_fd = -1;
		mutable std::function<void(std::u8string line)> _callback;
		mutable std::function<void(std::u8string line)> _err_callback;
		std::jthread _reader;
		int _read_fd = -1;
		int _write_fd = -1;

	public:
		~output_redirect();

		void callback(std::function<void(std::u8string line)> callback) { _callback = callback; }
		std::function<void(std::u8string line)> callback() const { return _callback; }

		void err_callback(std::function<void(std::u8string line)> callback) { _err_callback = callback; }
		std::function<void(std::u8string line)> err_callback() const { return _err_callback; }

		bool start(std::FILE* out);
		bool stop();
		int output_fd() const { return _out_alias_fd; }
	};
}

#endif
