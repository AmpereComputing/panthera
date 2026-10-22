//
// Copyright (c) 2022-2024 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// python extension module
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <iomanip>

#include "adis.h"

static PyObject* decode([[maybe_unused]] PyObject* self, PyObject* args) {
    std::uint64_t itext = 0;
    if (!PyArg_ParseTuple(args, "K", &itext)) {
        return NULL;
    }
    adis::encoding_enum_t r = adis::decode(static_cast<std::uint32_t>(itext));
    return Py_BuildValue("K", static_cast<unsigned long long>(r));
}


static PyObject* dis([[maybe_unused]] PyObject* self, PyObject* args) {
    std::uint64_t itext = 0;
    std::uint64_t va = 0;
    if (!PyArg_ParseTuple(args, "K|K", &itext, &va)) {
        return NULL;
    }
    const adis::encoding_enum_t r = adis::decode(static_cast<std::uint32_t>(itext));
    std::ostringstream os;
    os << adis::disasm(itext, r, va);
    return Py_BuildValue("s", os.str().c_str());
}


static PyObject* int2encoding_enum([[maybe_unused]] PyObject* self, PyObject* args) {
    std::uint64_t inum = 0;
    if (!PyArg_ParseTuple(args, "K", &inum)) {
        return NULL;
    }
    adis::encoding_enum_t r = static_cast<adis::encoding_enum_t>(inum);
    std::ostringstream os;
    os << adis::encoding_name(r);
    return Py_BuildValue("s", os.str().c_str());
}


static PyObject* str2encoding_enum([[maybe_unused]] PyObject* self, PyObject* args) {
    char*  enc_name  = 0;
    if (!PyArg_ParseTuple(args, "s", &enc_name)) {
        return NULL;
    }
    adis::encoding_enum_t r = adis::str2encoding_enum(enc_name);
    return Py_BuildValue("k", static_cast<unsigned long long>(r));
}


static PyObject* is_sve_family([[maybe_unused]] PyObject* self, PyObject* args) {
    std::uint64_t inum = 0;
    if (!PyArg_ParseTuple(args, "K", &inum)) {
        return NULL;
    }
    const adis::encoding_enum_t r = static_cast<adis::encoding_enum_t>(inum);
    bool retval = false;
    if (adis::sve_family(r)) {
        retval = true;
    }
    return Py_BuildValue("O", retval ? Py_True: Py_False);
}


static PyObject* is_sve([[maybe_unused]] PyObject* self, PyObject* args) {
    // only SVE, not SVE2, etc.
    std::uint64_t inum = 0;
    if (!PyArg_ParseTuple(args, "K", &inum)) {
        return NULL;
    }
    const adis::encoding_enum_t r = static_cast<adis::encoding_enum_t>(inum);
    bool retval = false;
    if (adis::sve(r)) {
        retval = true;
    }
    return Py_BuildValue("O", retval ? Py_True: Py_False);
}


static PyObject* is_sve2([[maybe_unused]] PyObject* self, PyObject* args) {
    std::uint64_t inum = 0;
    if (!PyArg_ParseTuple(args, "K", &inum)) {
        return NULL;
    }
    const adis::encoding_enum_t r = static_cast<adis::encoding_enum_t>(inum);
    bool retval = false;
    if (adis::sve2(r)) {
        retval = true;
    }
    return Py_BuildValue("O", retval ? Py_True: Py_False);
}


static PyMethodDef adis_methods[] = {
    {"decode", decode, METH_VARARGS,
     "Decode an instruction, returns encoding enum as an int"},
    {"dis", dis, METH_VARARGS,
     "Disassemble an instruction (and optional virtual address)"},
    {"is_sve_family", is_sve_family, METH_VARARGS,
     "Is the encoding enum int SVE/SVE2/etc, return bool"},
    {"is_sve", is_sve, METH_VARARGS,
     "Is the encoding enum int SVE (not SVE2), return bool"},
    {"is_sve2", is_sve2, METH_VARARGS,
     "Is the encoding enum int (not SVE), return bool"},
    {"str2encoding_enum", str2encoding_enum, METH_VARARGS,
     "Convert an encoding string name to the encoding enum value, as int"},
    {"int2encoding_enum", int2encoding_enum, METH_VARARGS,
     "Convert an integer name to the encodingenum string"},
    { NULL, NULL, 0, NULL}
};

static struct PyModuleDef adis_module;

PyMODINIT_FUNC
PyInit_adis(void) {
    adis_module.m_base = PyModuleDef_HEAD_INIT;
    adis_module.m_name = "adis";
    adis_module.m_doc = "ADIS - ARMv8/v9 disassembler";
    adis_module.m_size = -1;
    adis_module.m_methods = adis_methods;
    return PyModule_Create(&adis_module);
}
