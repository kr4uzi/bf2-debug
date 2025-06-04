#include "dis.h"
#include <set>
#include <unordered_map>
#include <algorithm>
#include <map>
#include <vector>
#include <span>
#include <print>
#include <format>

int HAVE_ARGUMENT = -1;
int EXTENDED_ARG = -1;
std::set<int> hasjrel;
std::set<int> hasjabs;
std::set<int> hasname;
std::set<int> haslocal;
std::set<int> hasfree;
std::set<int> hascompare;
std::set<int> hasconst;
std::vector<std::string> cmp_op;
std::unordered_map<int, std::string> opname;

bool dist_init()
{
    if (PyRun_SimpleString("import sys\nsys.path = ['pylib-2.3.4.zip']")) {
        PyErr_Print();
        return false;
    }

    char moduleName[] = "opcode";
	auto opcodes = PyImport_ImportModule(moduleName);
	if (!opcodes) {
		std::println("failed to import opcodes");
		return false;
	}

	auto dict = PyModule_GetDict(opcodes);
	if (!dict) {
		std::println("failed to get opcodes dict");
		return false;
	}

    std::map<const char*, int*> scalars = {
        { "HAVE_ARGUMENT", &HAVE_ARGUMENT },
        { "EXTENDED_ARG", &EXTENDED_ARG }
    };

    for (const auto& [name, val] : scalars) {
        auto obj = PyDict_GetItemString(dict, name);
        if (!obj) {
            std::println("failed to get {} from opcode", name);
            return false;
        }

        *val = PyInt_AS_LONG(obj);
    }

    std::map<const char*, std::set<int>*> lists = {
        { "hasjrel", &hasjrel },
        { "hasjabs", &hasjabs },
        { "hasname", &hasname },
        { "haslocal", &haslocal },
        { "hasfree", &hasfree },
        { "hascompare", &hascompare },
        { "hasconst", &hasconst }
    };

    for (const auto& [name, list] : lists) {
        auto obj = PyDict_GetItemString(dict, name);
        if (!obj) {
            std::println("failed to get {} from opcode", name);
            return false;
        }

        for (int i = 0, size = PyList_GET_SIZE(obj); i < size; i++) {
            auto item = PyList_GET_ITEM(obj, i);
            list->insert(PyInt_AS_LONG(item));
        }
    }

    auto cmp_opObj = PyDict_GetItemString(dict, "cmp_op");
    if (!cmp_opObj) {
        std::println("failed to get cmp_op from opcode");
        return false;
    }

    for (int i = 0, size = PyTuple_GET_SIZE(cmp_opObj); i < size; i++) {
        auto item = PyTuple_GET_ITEM(cmp_opObj, i);
        auto name = PyString_AS_STRING(item);
        cmp_op.push_back(name);
    }

	auto opnameObj = PyDict_GetItemString(dict, "opname");
    if (!opnameObj) {
        std::println("failed to get opname from opcode");
        return false;
    }

	for (int i = 0, size = PyList_GET_SIZE(opnameObj); i < size; i++) {
		auto item = PyList_GET_ITEM(opnameObj, i);
		auto name = PyString_AS_STRING(item);
		opname[i] = name;
	}

	return true;
}

std::vector<int> findlabels(const std::span<char>& code)
{
    std::vector<int> labels;
    auto n = code.size();
    std::size_t i = 0;
    while (i < n) {
        auto op = code[i];
        i++;
        if (op >= HAVE_ARGUMENT) {
            if ((i + 1) >= code.size()) {
                // code path is only chosen if HAVE_ARGUMENT is not initialized (which doesn't happen if dist_init was called)
                break;
            }
            auto oparg = code[i] + code[i + 1] * 256;
            i += 2;
            auto label = -1;
            if (hasjrel.contains(op)) {
                label = i + oparg;
            }
            else if (hasjabs.contains(op)) {
                label = oparg;
            }

            if (label >= 0) {
                if (!std::ranges::contains(labels, label)) {
                    labels.push_back(label);
                }
            }
        }
    }

    return labels;
}

std::string dis(PyCodeObject* co, int lasti)
{
    std::span<char> code; // points to the internal string buffer of co_code
    {
        char* str; int str_size;
        int err = PyString_AsStringAndSize(co->co_code, &str, &str_size);
        if (err) {
            return "";
        }

        code = std::span<char>(str, str_size);
    }
    
    std::span<char> co_lnotab; // points to the internal string buffer of co_lnotab
    {
        char* str; int str_size;
        int err = PyString_AsStringAndSize(co->co_lnotab, &str, &str_size);
        if (err) {
            return "";
        }

        co_lnotab = std::span<char>(str, str_size);
    }

    std::vector<char> byte_increments;
    std::vector<char> line_increments;
    for (std::size_t i = 0, len = co_lnotab.size(); i < len; ++i) {
        if (i % 2 == 0) {
            byte_increments.push_back(co_lnotab[i]);
        }
        else {
            line_increments.push_back(co_lnotab[i]);
        }
    }
    auto table_length = byte_increments.size(); //  == len(line_increments)

    auto lineno = co->co_firstlineno;
    auto table_index = 0;

    while (table_index < table_length && byte_increments[table_index] == 0) {
        lineno += line_increments[table_index];
        table_index += 1;
    }
    auto addr = 0;
    auto line_incr = 0;
    
    std::string res;
    auto labels = findlabels(code);
    auto n = code.size();
    auto i = 0;
    auto extended_arg = 0;
    PyObject* freev = nullptr;
    while (i < n) {
        auto op = code[i];

        if (i >= addr) {
            lineno += line_incr;

            bool full = true;
            while (table_index < table_length) {
                addr += byte_increments[table_index];
                line_incr = line_increments[table_index];
                table_index += 1;
                if (line_incr) {
                    full = false;
                    break;
                }
            }
            
            if (full) {
                addr = std::numeric_limits<int>::max();
            }

            if (i > 0) {
                res += "\n";
            }

            res += std::format("{:3d}", lineno);
        }
        else {
            res += "   ";
        }

        if (i == lasti) res += "-->";
        else res += "   ";
        if (std::ranges::contains(labels, i)) res += ">>";
        else res += "  ";
        res += std::format("{:>4} ", i);
        res += std::format("{:<20}", opname[op]);
        i++;
        if (op >= HAVE_ARGUMENT) {
            auto oparg = code[i] + code[i + 1] * 256 + extended_arg;
            extended_arg = 0;
            i += 2;
            if (op == EXTENDED_ARG) {
                extended_arg = oparg * 65536L;
            }
            res += std::format("{:>5} ", oparg);

            PyObject* val = nullptr;
            if (hasconst.contains(op)) {
                val = PyTuple_GET_ITEM(co->co_consts, oparg);
            }
            else if (hasname.contains(op)) {
				val = PyTuple_GET_ITEM(co->co_names, oparg);
			}
            else if (hasjrel.contains(op)) {
				res += std::format("(to {})", i + oparg);
            }
            else if (haslocal.contains(op)) {
				val = PyTuple_GET_ITEM(co->co_varnames, oparg);
			}
            else if (hascompare.contains(op)) {
                res += std::format("({})", cmp_op[oparg]);
			}
            else if (hasfree.contains(op)) {
                if (freev == nullptr) {
					auto cellvarsize = PyTuple_GET_SIZE(co->co_cellvars);
					auto freevarsize = PyTuple_GET_SIZE(co->co_freevars);
                    freev = PyTuple_New(cellvarsize + freevarsize);
					for (int i = 0; i < cellvarsize; ++i) {
                        PyTuple_SET_ITEM(freev, i, PyTuple_GET_ITEM(co->co_cellvars, i));
					}

                    for (int i = 0; i < freevarsize; ++i) {
                        PyTuple_SET_ITEM(freev, cellvarsize + i, PyTuple_GET_ITEM(co->co_freevars, i));
                    }
                }

				assert(freev && "freev is never NULL");
                val = PyTuple_GET_ITEM(freev, oparg);
            }

            if (val) {
                auto vstr = PyObject_Str(val);
                res += std::format("({})", PyString_AsString(vstr));
				Py_DECREF(vstr);
            }
        }
		res += "\n";
    }

    Py_XDECREF(freev);

    return res;
}