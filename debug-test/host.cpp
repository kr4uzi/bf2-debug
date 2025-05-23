#include "host.h"
#include "python.h"
#include <print>
#include <type_traits>

PyObject* log_write(PyObject* self, PyObject* args)
{
	auto item = PyTuple_GetItem(args, 0);
	auto str = PyString_AsString(item);
	std::println("[host] {}", str);

	Py_INCREF(Py_None);
	return Py_None;
}

PyObject* pmgr_getPlayers(PyObject* self, PyObject* args)
{
	return PyList_New(0);
}

PyObject* registerHandler(PyObject* self, PyObject* args)
{
	Py_INCREF(Py_None);
	return Py_None;
}

PyObject* registerGameStatusHandler(PyObject* self, PyObject* args)
{
	Py_INCREF(Py_None);
	return Py_None;
}

PyObject* sgl_getModDirectory(PyObject* self, PyObject* args)
{
	return PyString_FromString("mods/bf2");
}

PyObject* sgl_setParam(PyObject* self, PyObject* args)
{
	Py_INCREF(Py_None);
	return Py_None;
}

PyObject* sgl_getParam(PyObject* self, PyObject* args)
{
	Py_INCREF(Py_None);
	return Py_None;
}

PyObject* sgl_getIsAIGame(PyObject* self, PyObject* args)
{
	Py_INCREF(Py_None);
	return Py_None;
}

PyObject* ss_getParam(PyObject* self, PyObject* args)
{
	Py_INCREF(Py_None);
	return Py_None;
}

#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION < 7
// const-ness was added in python 2.7
#define BF2PY_METHOD_NAME(name) (char*)name
#else
#define BF2PY_METHOD_NAME(name) name
#endif

PyMethodDef host_methods[] = {
	{ BF2PY_METHOD_NAME("log"), log_write, METH_VARARGS, nullptr },
	// rcon_invoke
	// rcon_feedback
	// pmgr_getNumberOfPlayers
	{ BF2PY_METHOD_NAME("pmgr_getPlayers"), pmgr_getPlayers, METH_VARARGS, nullptr },
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
	{ BF2PY_METHOD_NAME("registerHandler"), registerHandler, METH_VARARGS, nullptr },
	{ BF2PY_METHOD_NAME("registerGameStatusHandler"), registerGameStatusHandler, METH_VARARGS, nullptr },
	// unregisterGameStatusHandler
	// omgr_getObjectsOfType
	// omgr_getObjectsOfTemplate
	{ BF2PY_METHOD_NAME("sgl_getModDirectory"), sgl_getModDirectory, METH_VARARGS, nullptr },
	// sgl_getMapName
	// sgl_getWorldSize
	{ BF2PY_METHOD_NAME("sgl_setParam"), sgl_setParam, METH_VARARGS, nullptr },
	{ BF2PY_METHOD_NAME("sgl_getParam"), sgl_getParam, METH_VARARGS, nullptr },
	// sgl_endGame
	// sgl_getControlPointsInGroup
	{ BF2PY_METHOD_NAME("sgl_getIsAIGame"), sgl_getIsAIGame, METH_VARARGS, nullptr },
	// sgl_sendGameLogicEvent
	// sgl_sendPythonEvent
	// sgl_sendMedalEvent
	// sgl_sendRankEvent
	// sgl_sendHudEvent
	// sgl_sendTextMessage
	// gl_sendEndOfRoundData
	{ BF2PY_METHOD_NAME("ss_getParam"), ss_getParam, METH_VARARGS, nullptr },
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
	{ nullptr, nullptr, 0, nullptr }
};

bool init_host(FARPROC pyInitModule4)
{
	using init_t = std::decay_t<decltype(Py_InitModule4)>;
	char moduleName[] = "host";
	auto res = reinterpret_cast<init_t>(pyInitModule4)(moduleName, host_methods, nullptr, nullptr, PYTHON_API_VERSION);
	return res != nullptr;
}
