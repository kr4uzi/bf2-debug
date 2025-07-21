#include "output_redirect.h"
#include <print>
#include <cstdio>
#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#include <debugapi.h>
#define close _close
#define read _read
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define pipe _pipe
#else
#include <unistd.h>
#include <syslog.h>
#endif
using namespace bf2py;

// this function is inteded to be used to display critical error messages that might occur
// before the file_redirect has initialized
namespace {
    void show_error(const std::string& user_message)
    {
        char errmsg[2048];
        strerror_s(errmsg, errno);

        auto message = std::format("{}: {} {}", user_message, errno, errmsg);
#ifdef _WIN32
        ::OutputDebugStringA(message.c_str());
#else
        ::syslog(LOG_ERR, "%s: %s", message.c_str(), ::strerror(errno));
#endif

        std::println("{}", message);
    }
}

bool output_redirect::start(std::function<void(std::u8string line)> callback)
{
    if (_original_stdout != -1 && _original_stderr) {
        // already started
        return true;
    }

    _original_stdout = ::dup(::fileno(stdout));
    _original_stderr = ::dup(::fileno(stderr));
    if (_original_stdout == -1 || _original_stderr == -1) {
        ::show_error("dup failed for stdout or stderr");
        stop();
        return false;
    }

#ifdef _WIN32
    if (::pipe(_pipe, 4096, _O_BINARY) == -1) {
#else
    if (::pipe(_pipe) {
#endif
        ::show_error("pipe failed");
        stop();
        return false;
    }

    // we assume only utf8 strings are printed (so no std::wcout is used)
	// and stdout and stderr are newer used interleaved without a newline
    if (::dup2(_pipe[1], ::fileno(stdout))) {
        ::show_error("dup2 failed for stdout");
        stop();
        return false;
    }

    if (::dup2(_pipe[1], ::fileno(stderr))) {
        ::show_error("dup2 failed for stderr");
        stop();
        return false;
    }

    if (::setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
        ::show_error("setvbuf failed for stdout");
        stop();
        return false;
    }

    if (::setvbuf(stderr, nullptr, _IONBF, 0) != 0) {
        ::show_error("setvbuf failed for stderr");
        stop();
        return false;
    }

    _reader = std::jthread([this, callback](std::stop_token token) {
        std::u8string line;
        char8_t buffer[4096];
        while (!token.stop_requested()) {
            auto bytesRead = ::read(_pipe[0], buffer, sizeof(buffer));
            if (bytesRead <= 0) {
                break;
            }

            line.append(buffer, bytesRead);
            for (auto newLinePos = line.find('\n'); newLinePos != std::string::npos; newLinePos = line.find('\n')) {
                auto output = line.substr(0, newLinePos);
                line.erase(0, newLinePos + 1);
                callback(output);
            }
        }

        ::close(_pipe[0]);
    });

    return true;
}

bool output_redirect::stop()
{
    bool hasError = false;

    // close write pipe
    if (_pipe[1] != -1) {
        if (::close(_pipe[1]) == -1) {
            ::show_error("close failed (_output_fd[1])");
            hasError = hasError || true;
        }

        _pipe[1] = -1;
    }

    // restore stdout
    if (_original_stdout != -1) {
        if (::dup2(_original_stdout, ::fileno(stdout)) == -1) {
            ::show_error("dup2 failed for stdout");
            hasError = hasError || true;
        }
        else {
            if (::close(_original_stdout) == -1) {
                ::show_error("close failed (_original_stdout)");
                hasError = hasError || true;
            }
        }

        _original_stdout = -1;
    }

    // restore stderr
    if (_original_stderr != -1) {
        if (::dup2(_original_stderr, ::fileno(stderr)) == -1) {
            ::show_error("dup2 failed for stderr");
            hasError = hasError || true;
        }
        else {
            if (::close(_original_stderr) == -1) {
                ::show_error("close failed (_original_stderr)");
                hasError = hasError || true;
            }
        }

        _original_stderr = -1;
    }

    // stop output redirector thread
    if (_reader.joinable()) {
        _reader.request_stop();
        _reader.join();
    }

    // close read pipe (if not done by redirector thread - e.g. if start_redirect_output failed partially)
    if (_pipe[0] != -1) {
        if (::close(_pipe[0]) == -1) {
            ::show_error("close failed (_pipe[0])");
            hasError = hasError || true;
        }

        _pipe[0] = -1;
    }

    return !hasError;
}