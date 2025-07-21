#pragma once
#ifndef _BF2PY_DEBUG_FILE_REDIRECT_H_
#define _BF2PY_DEBUG_FILE_REDIRECT_H_

#include <thread>
#include <string>
#include <functional>

namespace bf2py {
	class output_redirect
	{
	public:
		bool start(std::function<void(std::u8string line)> callback);
		bool stop();

	private:
		std::jthread _reader;
		int _pipe[2] = { -1, -1 }; // read, write
		int _original_stdout = -1;
		int _original_stderr = -1;
	};
}

#endif
