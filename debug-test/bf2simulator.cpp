#include "bf2simulator.h"
#include "python.h"
#include <thread>
#include <type_traits>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <print>
#include <iostream>
#include <memory>
using namespace bf2py;
PyMethodDef host_methods[];

class DynLib
{
#ifdef _WIN32
	HMODULE _ptr;
#else
	void* _ptr;
#endif

public:
	DynLib(const char* path)
	{
#ifdef _WIN32
		_ptr = ::LoadLibraryA(path);
#else
#endif
	}

	~DynLib() {
		if (_ptr) {
#ifdef  _WIN32
			::FreeLibrary(_ptr);
#endif
		}
	}

	operator bool() const { return _ptr != nullptr; }
};

void bf2simulator::stop()
{
	_running = false;
}

int bf2simulator::run(const std::string& mod, const std::string& adminScript)
{
	if (_running) {
		return -1;
	}

	_running = true;

	std::vector<DynLib> dlls;
	for (const auto& path : _dlls) {
		dlls.emplace_back(path.c_str());
		if (!dlls.back()) {
			std::println("Failed to load dll '{}', error: {}", path, GetLastError());
			return 1;
		}
	}

#ifdef _WIN32
	// at this point bf2 will call ::AllocConsole
	::AllocConsole();
#endif

	// dice::hfe::GameServer::init
	// dice::hfe::python::PythonHost::initialize

	Py_NoSiteFlag = 1;
	Py_Initialize();
	struct _finalizer {
		~_finalizer() {
			if (PyErr_Occurred()) {
				PyErr_Print();
			}

			Py_Finalize();
		}
	} finalizer;

	// on lunch, the bf2 executable sets the detected mod path
	// Note: BF2 uses PyRun_SimpleString and *not* PySys_SetPath
	auto pathCommand = std::format("import sys\nsys.path = ['pylib-2.3.4.zip', 'python', 'mods/{}/python', 'admin']", mod);
	if (PyRun_SimpleString(pathCommand.c_str())) {
		PyErr_Print();
	}

	// the host module is initialized after the path is set
	using init_t = std::decay_t<decltype(Py_InitModule4)>;
	const auto bf2pyAPIVersion = 1012;
	auto hostInitRes = Py_InitModule4((char*)"host", host_methods, nullptr, nullptr, bf2pyAPIVersion);
	if (hostInitRes == nullptr) {
		std::println("Failed to initialize host module");
		PyErr_Print();
	}

	PyNewRef bf2Module = PyImport_ImportModule((char*)"bf2");
	if (!bf2Module) {
		std::println("Failed to import bf2 module");
		return 1;
	}

	// could use "PyObject_GetAttrString" but bf2 does it like this:
	auto bf2Dict = PyModule_GetDict(bf2Module);
	if (!bf2Dict) {
		std::println("Failed to get bf2 module dict");
		return 1;
	}

	auto playerConvFunc = PyDict_GetItemString(bf2Dict, "playerConvFunc");
	if (!playerConvFunc) {
		std::println("playerConvFunc missing");
		return 1;
	}

	auto initFunc = PyDict_GetItemString(bf2Dict, "init_module");
	if (!initFunc) {
		std::println("Failed to get init_module function");
		return 1;
	}

	if (!PyCallable_Check(initFunc)) {
		std::println("init_module is not a callable");
		return false;
	}

	PyNewRef initRes = PyObject_CallObject(initFunc, nullptr);
	if (!initRes) {
		std::println("Failed to call init_module function");
		return false;
	}

	// dice::hfe::python::PythonHost::initializeAdminMod
	PyNewRef adminModule = PyImport_ImportModule(const_cast<char*>(adminScript.c_str()));
	if (!adminModule) {
		std::println("Failed to import admin module");
		return 1;
	}

	auto adminDict = PyModule_GetDict(adminModule);
	if (!adminModule) {
		std::println("Failed to import admin module: no dict");
		return 1;
	}

	auto adminInit = PyDict_GetItemString(adminDict, "init");
	auto adminShutdown = PyDict_GetItemString(adminDict, "shutdown");
	auto adminUpdate = PyDict_GetItemString(adminDict, "update");
	if (!adminInit || !adminShutdown || !adminUpdate
		|| !PyCallable_Check(adminInit) || !PyCallable_Check(adminShutdown) || !PyCallable_Check(adminUpdate)) {
		std::println("admin: you must provide the functions init, shutdown and update -- can't enable admin");
		return 1;
	}

	PyNewRef adminInitRes = PyObject_CallFunction(adminInit, nullptr);
	if (!adminInitRes) {
		std::println("Failed to import admin module: init failed");
		return 1;
	}

	while (_running) {
		Py_BEGIN_ALLOW_THREADS
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		Py_END_ALLOW_THREADS

		// dice::hfe::python::PythonHost::update
		PyNewRef updateRes = PyObject_CallFunction(adminUpdate, nullptr);
		if (!updateRes) {
			std::println("error: admin update failed: ");
			return 1;
		}
	}

	// dice::hfe::python::PythonHost::shutdown
	PyNewRef shutdownRes = PyObject_CallFunction(adminShutdown, nullptr);
	if (!shutdownRes) {
		std::println("failed to shut down admin module: ");
		return 1;
	}

	return 0;
}

PyObject* log_write(PyObject* self, PyObject* args)
{
	auto item = PyTuple_GetItem(args, 0);
	auto str = PyString_AsString(item);
	std::println("[host] {}", str);

	Py_RETURN_NONE;
}

PyObject* pmgr_getPlayers(PyObject* self, PyObject* args)
{
	return PyList_New(0);
}

PyObject* registerHandler(PyObject* self, PyObject* args)
{
	Py_RETURN_NONE;
}

PyObject* registerGameStatusHandler(PyObject* self, PyObject* args)
{
	Py_RETURN_NONE;
}

PyObject* sgl_getModDirectory(PyObject* self, PyObject* args)
{
	return PyString_FromString("mods/bf2");
}

PyObject* sgl_setParam(PyObject* self, PyObject* args)
{
	Py_RETURN_NONE;
}

PyObject* sgl_getParam(PyObject* self, PyObject* args)
{
	Py_RETURN_NONE;
}

PyObject* sgl_getIsAIGame(PyObject* self, PyObject* args)
{
	Py_RETURN_NONE;
}

PyObject* ss_getParam(PyObject* self, PyObject* args)
{
	Py_RETURN_NONE;
}

PyMethodDef host_methods[] = {
	{ (char*)"log", log_write, METH_VARARGS, nullptr },
	// rcon_invoke
	// rcon_feedback
	// pmgr_getNumberOfPlayers
	{ (char*)"pmgr_getPlayers", pmgr_getPlayers, METH_VARARGS, nullptr },
	// pmgr_getCommander
	// pmgr_isIndexValid
	// pmgr_p_get
	// pmgr_p_set
	// pmgr_getScore
	// pmgr_setScore
	// pmgr_enableScoreEvents
	// timer_getWallTime
	// timer_getTimers
	// timer_created
	// timer_destroy
	{ (char*)"registerHandler", registerHandler, METH_VARARGS, nullptr },
	{ (char*)"registerGameStatusHandler", registerGameStatusHandler, METH_VARARGS, nullptr },
	// unregisterGameStatusHandler
	// omgr_getObjectsOfType
	// omgr_getObjectsOfTemplate
	{ (char*)"sgl_getModDirectory", sgl_getModDirectory, METH_VARARGS, nullptr },
	// sgl_getMapName
	// sgl_getWorldSize
	{ (char*)"sgl_setParam", sgl_setParam, METH_VARARGS, nullptr },
	{ (char*)"sgl_getParam", sgl_getParam, METH_VARARGS, nullptr },
	// sgl_endGame
	// sgl_getControlPointsInGroup
	{ (char*)"sgl_getIsAIGame", sgl_getIsAIGame, METH_VARARGS, nullptr },
	// sgl_sendGameLogicEvent
	// sgl_sendPythonEvent
	// sgl_sendMedalEvent
	// sgl_sendRankEvent
	// sgl_sendHudEvent
	// sgl_sendTextMessage
	// gl_sendEndOfRoundData
	{ (char*)"ss_getParam", ss_getParam, METH_VARARGS, nullptr },
	// sgl_getSettingsBool
	// sh_setEnableCommander
	// pers_plrAwardMedal
	// pers_plrSetUnlocks
	// pers_plrRequestUnlocks
	// pers_plrRequestStats
	// pers_plrRequestAwards
	// pers_getStatsKeyVal
	// pers_gamespyStatsSendSnapshotUpdate
	// pers_gamespyStatsSendSnapshotFinal
	// pers_gamespyStatsEndGame
	// pers_gamespyStatsNewGame
	// trig_create
	// trig_getObjects
	// trig_destroy
	// trig_destroyAll
	{ }
};
