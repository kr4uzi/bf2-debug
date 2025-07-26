#include "python.h"
#include <stdexcept>
#include <format>
#include <print>
using namespace bf2py;

namespace {
	PyObject* pyStringIOClass = nullptr;
}

struct py_print_wrapper {
	const char* _target;
	PyNewRef _newIO;
	PyObject* _oldIO = nullptr;

	py_print_wrapper(const char* target, PyObject* stringIO)
		: _target(target),
		_newIO(PyObject_CallObject(stringIO, nullptr)),
		_oldIO(PySys_GetObject(const_cast<char*>(target)))
	{
		if (!_newIO) {
			throw std::runtime_error{ std::format("failed to initialize StringIO for {}", target) };
		}

		if (PySys_SetObject(const_cast<char*>(target), _newIO) == -1) {
			throw std::runtime_error{ std::format("failed to set sys.{} to StringIO", target) };
		}
	}

	~py_print_wrapper()
	{
		if (PySys_SetObject(const_cast<char*>(_target), _oldIO) == -1) {
			std::println(stderr, "failed to restore sys.{}", _target);
		}
	}

	std::u8string str() {
		auto value = PyObject_CallMethod(_newIO, (char*)"getvalue", nullptr);
		if (value) {
			return reinterpret_cast<char8_t*>(PyString_AsString(value));
		}

		std::println(stderr, "StringIO::getvalue failed for {}", _target);
		return u8"";
	}
};

std::expected<py_call_result, std::u8string> py_utils::call(std::function<PyObject*()> callback)
{
	if (!pyStringIOClass) {
		return std::unexpected(u8"py_utils not initalized");
	}

	try {
		auto pyStdout = py_print_wrapper("stdout", pyStringIOClass);
		auto pyStderr = py_print_wrapper("stderr", pyStringIOClass);
		return py_call_result{
			.result = callback(),
			.out = pyStdout.str(),
			.err = pyStderr.str()
		};
	}
	catch (const std::runtime_error& e) {
		return std::unexpected(reinterpret_cast<const char8_t*>(e.what()));
	}
}

std::string py_utils::dis(PyCodeObject* co, int lasti)
{
	auto result = py_utils::call([&] -> PyObject* {
		PyNewRef disModule = PyImport_ImportModule((char*)"dis");
		PyNewRef disFunc = PyObject_GetAttrString(disModule, (char*)"disassemble");
		auto disRes = PyObject_CallFunction(disFunc, (char*)"Oi", co, lasti);
		if (!disRes) {
			PyErr_Print();
		}
		
		return disRes;
	});

	if (!result) {
		return reinterpret_cast<const char*>(result.error().c_str());
	}

	PyNewRef pyResult = result->result;
	if (!result->err.empty()) {
		return reinterpret_cast<const char*>(result->err.c_str());
	}

	if (!result->out.empty()) {
		return reinterpret_cast<const char*>(result->out.c_str());
	}

	return "Failed to dissassemble bytecode - sys.path might not yet be initialized";
}

bool py_utils::init()
{
	PyNewRef strIOModule = PyImport_ImportModule((char*)"StringIO");
	if (!strIOModule) {
		std::println(stderr, "failed to import StringIO");
		return false;
	}

	auto strIODict = PyModule_GetDict(strIOModule);
	if (!strIODict) {
		std::println(stderr, "failed to get StringIO dict");
		return false;
	}

	auto strIOClass = PyDict_GetItemString(strIODict, "StringIO");
	if (!strIOClass) {
		std::println(stderr, "failed to get StringIO class");
		return false;
	}

	::pyStringIOClass = strIOClass;
	return true;
}