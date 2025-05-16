#include "dis.h"
#include "../python/opcode.h"
#include <set>
#include <unordered_map>
#include <algorithm>

std::set<int> hasjrel = { FOR_ITER, JUMP_FORWARD, JUMP_IF_FALSE, JUMP_IF_TRUE, SETUP_LOOP, SETUP_EXCEPT, SETUP_FINALLY };
std::set<int> hasjabs = { JUMP_ABSOLUTE , CONTINUE_LOOP };
std::set<int> hasname = { STORE_NAME, DELETE_NAME, STORE_ATTR, DELETE_ATTR, STORE_GLOBAL, DELETE_GLOBAL, LOAD_NAME, LOAD_ATTR, IMPORT_NAME, IMPORT_FROM, LOAD_GLOBAL };
std::set<int> haslocal = { LOAD_FAST , STORE_FAST, DELETE_FAST };
std::set<int> hasfree = { LOAD_CLOSURE, LOAD_DEREF, STORE_DEREF };
std::set<int> hascompare = { COMPARE_OP };
std::set<int> hasconst = { LOAD_CONST };
std::vector<std::string> cmp_op = { "<", "<=", "==", "!=", ">", ">=", "in", "not in", "is", "is not", "exception match", "BAD" };
std::unordered_map<int, std::string> opname = {
    {  0, "STOP_CODE" },
    {  1, "POP_TOP" },
    {  2, "ROT_TWO" },
    {  3, "ROT_THREE" },
    {  4, "DUP_TOP" },
    {  5, "ROT_FOUR" },
    {  10, "UNARY_POSITIVE" },
    {  11, "UNARY_NEGATIVE" },
    {  12, "UNARY_NOT" },
    {  13, "UNARY_CONVERT" },
    {  15, "UNARY_INVERT" },
    {  19, "BINARY_POWER" },
    {  20, "BINARY_MULTIPLY" },
    {  21, "BINARY_DIVIDE" },
    {  22, "BINARY_MODULO" },
    {  23, "BINARY_ADD" },
    {  24, "BINARY_SUBTRACT" },
    {  25, "BINARY_SUBSCR" },
    {  26, "BINARY_FLOOR_DIVIDE" },
    {  27, "BINARY_TRUE_DIVIDE" },
    {  28, "INPLACE_FLOOR_DIVIDE" },
    {  29, "INPLACE_TRUE_DIVIDE" },
    {  30, "SLICE+0" },
    {  31, "SLICE+1" },
    {  32, "SLICE+2" },
    {  33, "SLICE+3" },
    {  40, "STORE_SLICE+0" },
    {  41, "STORE_SLICE+1" },
    {  42, "STORE_SLICE+2" },
    {  43, "STORE_SLICE+3" },
    {  50, "DELETE_SLICE+0" },
    {  51, "DELETE_SLICE+1" },
    {  52, "DELETE_SLICE+2" },
    {  53, "DELETE_SLICE+3" },
    {  55, "INPLACE_ADD" },
    {  56, "INPLACE_SUBTRACT" },
    {  57, "INPLACE_MULTIPLY" },
    {  58, "INPLACE_DIVIDE" },
    {  59, "INPLACE_MODULO" },
    {  60, "STORE_SUBSCR" },
    {  61, "DELETE_SUBSCR" },
    {  62, "BINARY_LSHIFT" },
    {  63, "BINARY_RSHIFT" },
    {  64, "BINARY_AND" },
    {  65, "BINARY_XOR" },
    {  66, "BINARY_OR" },
    {  67, "INPLACE_POWER" },
    {  68, "GET_ITER" },
    {  70, "PRINT_EXPR" },
    {  71, "PRINT_ITEM" },
    {  72, "PRINT_NEWLINE" },
    {  73, "PRINT_ITEM_TO" },
    {  74, "PRINT_NEWLINE_TO" },
    {  75, "INPLACE_LSHIFT" },
    {  76, "INPLACE_RSHIFT" },
    {  77, "INPLACE_AND" },
    {  78, "INPLACE_XOR" },
    {  79, "INPLACE_OR" },
    {  80, "BREAK_LOOP" },
    {  82, "LOAD_LOCALS" },
    {  83, "RETURN_VALUE" },
    {  84, "IMPORT_STAR" },
    {  85, "EXEC_STMT" },
    {  86, "YIELD_VALUE" },
    {  87, "POP_BLOCK" },
    {  88, "END_FINALLY" },
    {  89, "BUILD_CLASS" },
    {  90, "STORE_NAME" },
    {  91, "DELETE_NAME" },
    {  92, "UNPACK_SEQUENCE" },
    {  93, "FOR_ITER" },
    {  95, "STORE_ATTR" },
    {  96, "DELETE_ATTR" },
    {  97, "STORE_GLOBAL" },
    {  98, "DELETE_GLOBAL" },
    {  99, "DUP_TOPX" },
    {  100, "LOAD_CONST" },
    {  101, "LOAD_NAME" },
    {  102, "BUILD_TUPLE" },
    {  103, "BUILD_LIST" },
    {  104, "BUILD_MAP" },
    {  105, "LOAD_ATTR" },
    {  106, "COMPARE_OP" },
    {  107, "IMPORT_NAME" },
    {  108, "IMPORT_FROM" },
    {  110, "JUMP_FORWARD" },
    {  111, "JUMP_IF_FALSE" },
    {  112, "JUMP_IF_TRUE" },
    {  113, "JUMP_ABSOLUTE" },
    {  116, "LOAD_GLOBAL" },
    {  119, "CONTINUE_LOOP" },
    {  120, "SETUP_LOOP" },
    {  121, "SETUP_EXCEPT" },
    {  122, "SETUP_FINALLY" },
    {  124, "LOAD_FAST" },
    {  125, "STORE_FAST" },
    {  126, "DELETE_FAST" },
    {  130, "RAISE_VARARGS" },
    {  131, "CALL_FUNCTION" },
    {  132, "MAKE_FUNCTION" },
    {  133, "BUILD_SLICE" },
    {  134, "MAKE_CLOSURE" },
    {  135, "LOAD_CLOSURE" },
    {  136, "LOAD_DEREF" },
    {  137, "STORE_DEREF" },
    {  140, "CALL_FUNCTION_VAR" },
    {  141, "CALL_FUNCTION_KW" },
    {  142, "CALL_FUNCTION_VAR_KW" },
    {  143, "EXTENDED_ARG" }
};

bool dist_init()
{
    return true;
    auto initStr = std::format("import sys\nprint str(sys.path)\nsys.path = ['pylib-2.3.4.zip']");
    PyRun_SimpleString(initStr.c_str());

	auto opcodes = PyImport_ImportModule((char*)"opcodes");
	if (!opcodes) {
		std::println("failed to import opcodes");
		return false;
	}

	auto dict = PyModule_GetDict(opcodes);
	if (!dict) {
		std::println("failed to get opcodes dict");
		return false;
	}

	auto hasjrelObj = PyDict_GetItemString(dict, (char*)"hasjrel");
	if (!hasjrelObj) {
		std::println("failed to get hasjrel");
		return false;
	}

	auto size = PyList_GET_SIZE(hasjrelObj);
	for (int i = 0; i < size; i++) {
		auto item = PyList_GET_ITEM(hasjrelObj, i);
		hasjrel.insert(PyLong_AsLong(item));
	}

	auto hasjabsObj = PyDict_GetItemString(dict, (char*)"hasjabs");
	for (int i = 0, size = PyList_GET_SIZE(hasjabsObj); i < size; i++) {
		auto item = PyList_GET_ITEM(hasjabsObj, i);
		hasjabs.insert(PyLong_AsLong(item));
	}

	auto opnameObj = PyDict_GetItemString(dict, (char*)"opname");
	for (int i = 0, size = PyList_GET_SIZE(opnameObj); i < size; i++) {
		auto item = PyList_GET_ITEM(opnameObj, i);
		auto name = PyString_AsString(item);
		opname[i] = name;
	}

	return true;
}

std::vector<int> findlabels(const std::vector<char>& code)
{
    std::vector<int> labels;
    auto n = code.size();
    std::size_t i = 0;
    while (i < n) {
        auto op = code[i];
        i++;
        if (op >= HAVE_ARGUMENT) {
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
    std::vector<char> code;
    {
        char* str; int str_size;
        int err = PyString_AsStringAndSize(co->co_code, &str, &str_size);
        if (err) {
            return "";
        }

        code = std::vector<char>(str, str + str_size);
    }
    
    std::vector<char> co_lnotab;
    {
        char* str; int str_size;
        int err = PyString_AsStringAndSize(co->co_lnotab, &str, &str_size);
        if (err) {
            return "";
        }

        co_lnotab = std::vector<char>(str, str + str_size);
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