#define __STDC_WANT_LIB_EXT1__ 1
#include "output_redirect.h"
#include <string.h>
#include <format>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define close _close
#define read _read
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define pipe _pipe
#else
#include <unistd.h>
#endif
using namespace bf2py;

namespace {
    std::u8string last_error_message()
    {
        auto bufferSize = std::size_t(20248);
#ifdef __STDC_LIB_EXT1__
        bufferSize = strerrorlen_s(errno);
#endif

        auto buffer = std::vector<char8_t>(bufferSize + 1, '\0');
        if (strerror_s(reinterpret_cast<char*>(buffer.data()), buffer.size(), errno) == 0) {
            return buffer.data();
        }

        auto defaultMessage = std::format("error {}", errno);
        return std::u8string{ defaultMessage.begin(), defaultMessage.end() };
    }
}

output_redirect::~output_redirect()
{
    stop();
}

bool output_redirect::start(std::FILE* out)
{
    auto fd = ::fileno(out);
    if (fd == -1) {
        return false;
    }

	_out = out;
    _out_alias_fd = ::dup(fd);
    if (_out_alias_fd == -1) {
        _err_callback(last_error_message());
        return false;
    }

    int pipe_fd[2];
#ifdef _WIN32
    if (::pipe(pipe_fd, 4096, _O_BINARY) == -1) {
#else
    if (::pipe(pipe_fd) {
#endif
        ::close(_out_alias_fd);
        _err_callback(last_error_message());
        return false;
    }

    _read_fd = pipe_fd[0];
        _write_fd = pipe_fd[1];

        // close _out and replace it with our write pipe
        if (::dup2(_write_fd, fd)) {
            _err_callback(last_error_message());
            return false;
        }

    // disable buffering (this seems to be necessary at least on windows)
    if (::setvbuf(_out, nullptr, _IONBF, 0) != 0) {
        _err_callback(last_error_message());
        return false;
    }

    _reader = std::jthread([this](std::stop_token token) {
        std::u8string line;
        char8_t buffer[4096];
        while (!token.stop_requested()) {
            auto bytesRead = ::read(_read_fd, buffer, sizeof(buffer));
            if (bytesRead <= 0) {
                break;
            }

            line.append(buffer, bytesRead);
            for (auto newLinePos = line.find('\n'); newLinePos != std::string::npos && !token.stop_requested(); newLinePos = line.find('\n')) {
                auto output = line.substr(0, newLinePos);
                line.erase(0, newLinePos + 1);
                _callback(output);
            }
        }

        if (_read_fd != -1) {
            if (::close(_read_fd) == 0) {
                _read_fd = -1;
            }
            else {
                _err_callback(last_error_message());
            }
        }
    });

    return true;
}

bool output_redirect::stop()
{
    bool hasError = false;

    // make _reader end by canceling ::read
    _reader.request_stop();
    if (_write_fd != -1) {
        if (::close(_write_fd) == -1) {
            _err_callback(last_error_message());
            hasError = hasError || true;
        }

        _write_fd = -1;
    }

    // restore
    if (_out_alias_fd != -1) {
        if (::dup2(_out_alias_fd, ::fileno(_out)) == -1) {
            _err_callback(last_error_message());
            hasError = hasError || true;
        }

        if (::close(_out_alias_fd) == -1) {
            _err_callback(last_error_message());
            hasError = hasError || true;
        }

        _out_alias_fd = -1;
    }

    // stop output redirector thread
    if (_reader.joinable()) {
        _reader.join();
    }

    // the read pipe might still be open, e.g. if start failed partially
    if (_read_fd != -1) {
        if (::close(_read_fd) == -1) {
            _err_callback(last_error_message());
            hasError = hasError || true;
        }

        _read_fd = -1;
    }

    return !hasError;
}
