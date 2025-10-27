#include "bf2simulator.h"
#include "python.h"
#include <thread>
#include <type_traits>
#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <print>
using namespace bf2py;

void dyn_lib::dyn_lib_unloader::operator()(void* handle)
{
#ifdef  _WIN32
	::FreeLibrary(static_cast<HMODULE>(handle));
#else
	::dlclose(handle);
#endif
}

dyn_lib::dyn_lib(const char* path)
#ifdef _WIN32
	: _handle(::LoadLibraryA(path))
#else
	: _handle(::dlopen(path, RTLD_NOW))
#endif
{

}

void bf2simulator::stop()
{
	_running = false;
}

int bf2simulator::run()
{
	if (_running) {
		return -1;
	}

	_running = true;
	struct stopper {
		bf2simulator& _sim;
		~stopper() { _sim.stop(); }
	} stopper{ *this };

	std::vector<dyn_lib> dlls;
	for (const auto& path : _dlls) {
		dlls.emplace_back(path.c_str());
		if (!dlls.back()) {
			std::println("Failed to load dll '{}'", path);
			return 1;
		}
	}

#ifdef _WIN32
	// at this point bf2 will call ::AllocConsole
	::AllocConsole();
#endif

	if (!PythonHost_initialize()) {
		std::println("failed to initialize python");
		return 1;
	}

	if (!PythonHost_initializeAdminMod()) {
		std::println("failed to initialize admin module");
		return 1;
	}	

	int exitCode = 0;
	while (_running) {
		Py_BEGIN_ALLOW_THREADS
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		Py_END_ALLOW_THREADS

		// dice::hfe::python::PythonHost::update
		PyNewRef updateRes = PyObject_CallFunction(static_cast<PyObject*>(_adminUpdate), nullptr);
		if (!updateRes) {
			std::println("error: admin update failed: ");
			exitCode = 1;
			break;
		}
	}

	if (PythonHost_shutdown()) {
		exitCode = 1;
	}

	return exitCode;
}

bool bf2simulator::PythonHost_initialize()
{
	Py_NoSiteFlag = 1;
	Py_Initialize();

	// on lunch, the bf2 executable sets the detected mod path
	// Note: BF2 uses PyRun_SimpleString and *not* PySys_SetPath
	auto pathCommand = std::format("import sys\nsys.path = ['pylib-2.3.4.zip', 'python', 'mods/{}/python', 'admin']", _mod);
	if (PyRun_SimpleString(pathCommand.c_str())) {
		PyErr_Print();
	}

	if (!createHostModule()) {
		PyErr_Print();
	}

	PyNewRef bf2Module = PyImport_ImportModule((char*)"bf2");
	if (!bf2Module) {
		std::println("Failed to import bf2 module");
		return false;
	}

	// could use "PyObject_GetAttrString" but bf2 does it like this:
	auto bf2Dict = PyModule_GetDict(bf2Module);
	if (!bf2Dict) {
		std::println("Failed to get bf2 module dict");
		return false;
	}

	auto playerConvFunc = PyDict_GetItemString(bf2Dict, "playerConvFunc");
	if (!playerConvFunc) {
		std::println("playerConvFunc missing");
		return false;
	}

	// bf2 tests the playerConvFunc at this point

	auto initFunc = PyDict_GetItemString(bf2Dict, "init_module");
	if (!initFunc) {
		std::println("Failed to get init_module function");
		return false;
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

	return true;
}

bool bf2simulator::PythonHost_initializeAdminMod()
{
	PyNewRef adminModule = PyImport_ImportModule(const_cast<char*>(_adminScript.c_str()));
	if (!adminModule) {
		std::println("Failed to import admin module");
		return false;
	}

	auto adminDict = PyModule_GetDict(adminModule);
	if (!adminModule) {
		std::println("Failed to import admin module: no dict");
		return false;
	}

	auto adminInit = PyDict_GetItemString(adminDict, "init");
	auto adminShutdown = PyDict_GetItemString(adminDict, "shutdown");
	auto adminUpdate = PyDict_GetItemString(adminDict, "update");
	if (!adminInit || !adminShutdown || !adminUpdate
		|| !PyCallable_Check(adminInit) || !PyCallable_Check(adminShutdown) || !PyCallable_Check(adminUpdate)) {
		std::println("admin: you must provide the functions init, shutdown and update -- can't enable admin");
		return false;
	}

	PyNewRef adminInitRes = PyObject_CallFunction(adminInit, nullptr);
	if (!adminInitRes) {
		std::println("Failed to import admin module: init failed");
		return false;
	}

	_adminInit = adminInit;
	_adminShutdown = adminShutdown;
	_adminUpdate = adminUpdate;
	return true;
}

bool bf2simulator::createHostModule()
{
	static bf2simulator* instance = this;
	static PyMethodDef host_methods[] = {
		{ (char*)"log", [](PyObject * self, PyObject * args)
			{
				auto item = PyTuple_GetItem(args, 0);
				auto str = PyString_AsString(item);
				std::println("[host] {}", str);

				Py_RETURN_NONE;
			}, METH_VARARGS, nullptr
		},
		// rcon_invoke
		// rcon_feedback
		// pmgr_getNumberOfPlayers
		{ (char*)"pmgr_getPlayers", [](PyObject * self, PyObject * args)
			{
				return PyList_New(0);
			}, METH_VARARGS, nullptr
		},
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
		{ (char*)"registerHandler", [](PyObject * self, PyObject * args)
			{
				Py_RETURN_NONE;
			}, METH_VARARGS, nullptr
		},
		{ (char*)"registerGameStatusHandler", [](PyObject * self, PyObject * args)
			{
				Py_RETURN_NONE;
			}, METH_VARARGS, nullptr
		},
		// unregisterGameStatusHandler
		// omgr_getObjectsOfType
		// omgr_getObjectsOfTemplate
		{ (char*)"sgl_getModDirectory", [](PyObject * self, PyObject * args)
			{
				auto modPath = std::format("mods/{}", instance->_mod);
				return PyString_FromString(modPath.c_str());
			}, METH_VARARGS, nullptr
		},
		// sgl_getMapName
		// sgl_getWorldSize
		{ (char*)"sgl_setParam", [](PyObject* self, PyObject* args)
			{
				Py_RETURN_NONE;
			}, METH_VARARGS, nullptr
		},
		{ (char*)"sgl_getParam", [](PyObject* self, PyObject* args)
			{
				Py_RETURN_NONE;
			}, METH_VARARGS, nullptr
		},
		// sgl_endGame
		// sgl_getControlPointsInGroup
		{ (char*)"sgl_getIsAIGame", [](PyObject* self, PyObject* args)
			{
				Py_RETURN_NONE;
			}, METH_VARARGS, nullptr
		},
		// sgl_sendGameLogicEvent
		// sgl_sendPythonEvent
		// sgl_sendMedalEvent
		// sgl_sendRankEvent
		// sgl_sendHudEvent
		// sgl_sendTextMessage
		// gl_sendEndOfRoundData
		{ (char*)"ss_getParam", [](PyObject* self, PyObject* args)
			{
				Py_RETURN_NONE;
			}, METH_VARARGS, nullptr
		},
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

	const auto bf2pyAPIVersion = 1012;
	auto hostInitRes = Py_InitModule4((char*)"host", host_methods, nullptr, nullptr, bf2pyAPIVersion);
	if (hostInitRes == nullptr) {
		std::println("Failed to initialize host module");
		return false;
	}

	return true;
}

bool bf2simulator::PythonHost_shutdown()
{
	bool success = true;
	if (Py_IsInitialized()) {
		if (PyErr_Occurred()) {
			PyErr_Print();
		}

		if (auto adminShutdown = static_cast<PyObject*>(_adminShutdown); adminShutdown) {
			PyNewRef shutdownRes = PyObject_CallFunction(adminShutdown, nullptr);
			if (!shutdownRes) {
				std::println("failed to shut down admin module: ");
				PyErr_Print();
				success = false;
			}
		}	

		Py_Finalize();
	}

	return success;
}