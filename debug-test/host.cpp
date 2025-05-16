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

PyMethodDef host_methods[] = {
	{ (char*)"log", log_write, METH_VARARGS, nullptr },
	// rcon_invoke
	// rcon_feedback
	// pmgr_getNumberOfPlayers
	// pmgr_getPlayers
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
	// registerHandler
	// registerGameStatusHandler
	// unregisterGameStatusHandler
	// omgr_getObjectsOfType
	// omgr_getObjectsOfTemplate
	// sgl_getModDirectory
	// sgl_getMapName
	// sgl_getWorldSize
	// sgl_setParam
	// sgl_getParam
	// sgl_endGame
	// sgl_getControlPointsInGroup
	// sgl_getIsAIGame
	// sgl_sendGameLogicEvent
	// sgl_sendPythonEvent
	// sgl_sendMedalEvent
	// sgl_sendRankEvent
	// sgl_sendHudEvent
	// sgl_sendTextMessage
	// gl_sendEndOfRoundData
	// ss_getParam
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
	typedef PyObject* (*pyInitModule4_func)(char*, PyMethodDef*, char*, PyObject*, int);
	static_assert(std::is_same_v<pyInitModule4_func, decltype(&Py_InitModule4)>, "Py_InitModule4 signatures must match");
	auto res = reinterpret_cast<PyObject* (*)(char*, PyMethodDef*, char*, PyObject*, int)>(pyInitModule4)(
		(char*)"host", host_methods, nullptr, nullptr, PYTHON_API_VERSION
	);
	return res != nullptr;
}
