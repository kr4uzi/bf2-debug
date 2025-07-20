#include "dis.h"

std::string dis(PyCodeObject* co, int lasti)
{
    std::string codeStr;
    auto strIOModule = PyImport_ImportModule((char*)"StringIO");
    auto disModule = PyImport_ImportModule((char*)"dis");

    if (strIOModule && disModule) {
        auto strIOClass = PyObject_GetAttrString(strIOModule, (char*)"StringIO");
        auto dis = PyObject_GetAttrString(disModule, (char*)"disassemble");
        if (strIOClass && dis) {
            auto strIO = PyObject_CallObject(strIOClass, nullptr);
            if (strIO) {
                PyObject* oldStdout = PySys_GetObject((char*)"stdout");
                PySys_SetObject((char*)"stdout", strIO);

                auto disRes = PyObject_CallFunction(dis, (char*)"Oi", co, lasti);
                if (disRes) {
                    Py_DECREF(disRes);
                }
                else {
                    PyErr_Print();
                }

                PySys_SetObject((char*)"stdout", oldStdout);

                auto disCode = PyObject_CallMethod(strIO, (char*)"getvalue", nullptr);
                if (disCode) {
                    codeStr = PyString_AS_STRING(disCode);
                    Py_DECREF(disCode);
                }
            }

            Py_XDECREF(strIO);
        }

        Py_XDECREF(dis);
        Py_XDECREF(strIOClass);
    }

    if (codeStr.empty()) {
        codeStr = "Failed to dissassemble bytecode - sys.path might not yet be initialized";
    }

    Py_XDECREF(disModule);
    Py_XDECREF(strIOModule);
	return codeStr;
}