//
// Copyright(c) 2022 Ampere Computing. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
#include <cassert>
#include <cstddef>
#include <list>
#include <ranges>

#include "ampere/event_stream/event_stream.pb.h"
#include "ampere/evp/processor_ifc.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "ampere/evp/plugin.h"
#include "ampere/event_stream/event_stream_reader.h"

namespace ampere::evp {

/** The Python plugin class.
 *
 *  Most of the actual work by this plugin is handled by a single global/shared
 *  instance of PythonPluginHelper, below.
 *
 *  There are four APIs exposed to a user's Python file today:
 *
 *    1. on             : react to an event (pattern) by calling a function
 *    2. end_simulation : react to the end of simulation by calling a function
 *    3. collect        : schedule a function to call on collect time points
 *    4. has_definition : report whether an event is defined (by name)
 */
AMPERE_EVP_PLUGIN(Python, "python", "Custom python-based event processor plugin")
{
public:
    Python(ProcessorIfc & proc_ifc, Args & args);
    ~Python() override;

    void define_value(const Definition& definition) override;

    void collect(MetricSeries & metrics, uint64_t trigger_time) override;
    void end_simulation() override;

private:
    unsigned python_plugin_idx;
};

class PythonPluginHelper : Plugin
{
public:
    PythonPluginHelper(ProcessorIfc& proc_ifc);
    ~PythonPluginHelper() override;

    void define_value(const Definition& definition) override;

    void collect(MetricSeries& metrics, uint64_t trigger_time) override;
    void end_simulation() override;

    void py_on(const std::string& trigger, PyObject* func);
    bool py_has_definition(const std::string& name);
    const Parameter* py_get_parameter(const std::string& name);
    void py_collect(PyObject* func) { collect_functions.push_back(func); }
    void py_end_simulation(PyObject* func) { end_simulation_functions.push_back(func); }

    ProcessorIfc* get_proc_ifc() { return &proc_ifc; }

    PyObject* get_enum_type(const Definition& definition) { return enumerations.at(definition.enumeration_id()); }

private:
    static PyObject* create_enumeration(const std::string& name, const Enumeration& enum_def);

    std::map<int, PyObject*> enumerations;
    std::list<PyObject*> collect_functions;
    std::list<PyObject*> end_simulation_functions;
};

static PythonPluginHelper* python_plugin_helper = nullptr;
static unsigned number_python_plugins = 0;
static PyObject* enumeration_aliases = nullptr;

/** Utility function for reporting errors with Python APIs return NULL (Python's
 *  way of reporting an exception). It will print a stack backtrace and throw
 *  an exception to exit.
 */
static void py_err(const std::string& msg)
{
    PyErr_Print();
    throw std::runtime_error{msg};
}

/** Global function implementing the "on" hook.
 *
 *  We do the Pythonic processing here, and then springboard into the plugin
 *  method `py_on` to ultimatly reach the `on` C++ method.
 */
static PyObject* evp_on(PyObject* self, PyObject* args)
{
    const char* trigger;
    PyObject* func;

    // Argument example: on("event.name", call_me)
    //
    if (!PyArg_ParseTuple(args, "sO:on", &trigger, &func))
        return nullptr;

    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "second argument to \"on\" must be callable");
        return nullptr;
    }

    Py_INCREF(func);
    python_plugin_helper->py_on(trigger, func);

    Py_RETURN_NONE;
}

/** Global function implementing the "has_definition" hook.
 *
 *  We do the Pythonic processing here, and then springboard into the plugin
 *  method `py_has_definition` to ultimately reach the `on` C++ method.
 */
static PyObject* evp_has_definition(PyObject* self, PyObject* args)
{
    const char* name;

    // Argument example: has_definition("event.name")
    //
    if (!PyArg_ParseTuple(args, "s:has_definition", &name))
        return nullptr;

    if (python_plugin_helper->py_has_definition(name))
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

/** Global function implementing the "get_parameter" hook.
 *
 *  We do the Pythonic processing here, and then springboard into the plugin
 *  method `py_get_parameter` to ultimately reach the `on` C++ method. If a
 *  parameter does not exist, None will be returned.
 */
static PyObject* evp_get_parameter(PyObject* self, PyObject* args)
{
    const char* name;

    // Argument example: get_parameter("name")
    //
    if (!PyArg_ParseTuple(args, "s:get_parameter", &name))
        return nullptr;

    if (const auto* param = python_plugin_helper->py_get_parameter(name); param) {
        switch (param->value_case()) {
        case ampere::event_stream_proto::Parameter::kBoolValue:
            if (param->bool_value())
                Py_RETURN_TRUE;
            Py_RETURN_FALSE;
        case ampere::event_stream_proto::Parameter::kUintValue:
            return Py_BuildValue("K", param->uint_value());
        case ampere::event_stream_proto::Parameter::kIntValue:
            return Py_BuildValue("L", param->int_value());
        case ampere::event_stream_proto::Parameter::kDoubleValue:
            return Py_BuildValue("d", param->double_value());
        case ampere::event_stream_proto::Parameter::kStringValue:
            return Py_BuildValue("s", param->string_value().c_str());
        case ampere::event_stream_proto::Parameter::kJsonValue: {
            auto* json_string = Py_BuildValue("s", param->json_value().c_str());
            auto* json = PyImport_ImportModule("json");
            auto* loads = PyObject_GetAttrString(json, "loads");
            return PyObject_CallOneArg(loads, json_string);
        }
        default:
            break;
        }
    }

    Py_RETURN_NONE;
}

/** Global function implementing the "collect" hook.
 *
 *  Really just save away the function for later.
 */
static PyObject* evp_collect(PyObject* self, PyObject* args)
{
    PyObject* func;

    if (!PyArg_ParseTuple(args, "O:collect", &func))
        return nullptr;

    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "first argument to \"collect\" must be callable");
        return nullptr;
    }

    Py_INCREF(func);
    python_plugin_helper->py_collect(func);

    Py_RETURN_NONE;
}

/** Global function implementing the "end_simulation" hook.
 *
 *  We do the Pythonic processing here, and then springboard into the plugin
 *  method `py_end_simulation`.
 */
static PyObject* evp_end_simulation(PyObject* self, PyObject* args)
{
    PyObject* func;

    if (!PyArg_ParseTuple(args, "O:end_simulation", &func))
        return nullptr;

    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "first argument to \"end_simulation\" must be callable");
        return nullptr;
    }

    Py_INCREF(func);
    python_plugin_helper->py_end_simulation(func);

    Py_RETURN_NONE;
}

/** Python "Event" class.
 *
 *  The callback function for `on` receives one of these. It is a simple object with these
 *  fields:
 *
 *    name  - full name of the event
 *    time  - timestamp of the event (picoseconds)
 *    data  - dictionary of all data items
 *
 *  The data dictionary deserves a bit more explanation. It is expensive to build this, and
 *  there are cases when the user's code won't care; maybe it's just counting event occurrences
 *  or some such. So we try to delay building it until it gets accessed. We have to build it
 *  from the protobuf data structure though, and it would be similarly expensive to keep that
 *  around. The solution here is to *temporarily* squirrel away a pointer to the protobuf info
 *  for the duration of the callback given to `on`. If the data is accessed in that time, then
 *  we'll build the dictionary. If not, then we'll null out the protobuf pointer and data will
 *  forever be an empty dictionary.
 */
struct EventObject
{
    PyObject_HEAD PyObject* name;
    uint64_t time;
    PyObject* data;

    const event_stream_proto::Event* _tmp_proto;

    // Called when we first construct an Event
    void from_proto(const event_stream_proto::Event& proto_event, ProcessorIfc& proc_ifc)
    {
        name = Py_BuildValue("s", proc_ifc.get_definition(proto_event.definition_id()).name().c_str());
        time = proto_event.time();
        data = nullptr;

        _tmp_proto = &proto_event;
    }

    // Called after the `on` callback returns, because our protobuf info is soon to become
    // invalid.
    void clear_proto() { _tmp_proto = nullptr; }

    // Build the Python dictionary from protobuf if it doesn't already exist. This is the
    // "getter" function for this attribute.
    void build_data(ProcessorIfc* proc_ifc)
    {
        if (data)
            return;

        data = Py_BuildValue("{}");

        if (!_tmp_proto)
            return;

        for (int i = 0; i < _tmp_proto->values_size(); ++i) {
            const auto& value = _tmp_proto->values(i);
            const auto& definition = proc_ifc->get_definition(value.definition_id());
            const auto& name = definition.name();
            auto type = value.values_case();

            PyObject* key = PyUnicode_InternFromString(name.c_str());
            PyObject* val = nullptr;

            if (type == event_stream_proto::Value::kIntValue) {
                if (proc_ifc->has_enumeration(definition)) {
                    auto* enum_type = python_plugin_helper->get_enum_type(definition);
                    PyObject* val_args = Py_BuildValue("(L)", value.int_value());
                    val = PyObject_CallObject(enum_type, val_args);
                    Py_DECREF(val_args);
                } else {
                    val = PyLong_FromLong(value.int_value());
                }
            } else if (type == event_stream_proto::Value::kUintValue) {
                if (proc_ifc->has_enumeration(definition)) {
                    auto* enum_type = python_plugin_helper->get_enum_type(definition);
                    PyObject* val_args = Py_BuildValue("(K)", value.uint_value());
                    val = PyObject_CallObject(enum_type, val_args);
                    Py_DECREF(val_args);
                } else {
                    val = PyLong_FromUnsignedLong(value.uint_value());
                }
            } else if (type == event_stream_proto::Value::kStringValue) {
                val = PyUnicode_FromString(value.string_value().c_str());
            }

            if (PyDict_SetItem(data, key, val) < 0)
                py_err("failed to build data dictionary");

            if (auto dot = name.rfind('.'); dot != std::string::npos) {
                auto short_key = name.substr(dot + 1);
                PyObject* short_key_obj = PyUnicode_InternFromString(short_key.c_str());

                if (PyDict_SetItem(data, short_key_obj, val) < 0)
                    py_err("failed to build data dictionary");
            }

            Py_XDECREF(key);
            Py_XDECREF(val);
        }
    }
};

//
// Here begins the Python rigamarole for defining the Event object and its
// associated accessors.
//

static void EventObject_dealloc(EventObject* self)
{
    Py_XDECREF(self->name);
    Py_XDECREF(self->data);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* EventObject_get_name(EventObject* self, void* closure)
{
    Py_INCREF(self->name);
    return self->name;
}

static PyObject* EventObject_get_time(EventObject* self, void* closure)
{
    return PyLong_FromUnsignedLongLong(self->time);
}

static PyObject* EventObject_get_data(EventObject* self, void* closure)
{
    self->build_data(python_plugin_helper->get_proc_ifc());
    Py_INCREF(self->data);
    return self->data;
}

static PyGetSetDef EventObject_getsetters[] = {
    {"name", (getter)EventObject_get_name, nullptr, "event name", nullptr},
    {"time", (getter)EventObject_get_time, nullptr, "event time", nullptr},
    {"data", (getter)EventObject_get_data, nullptr, "event data values", nullptr},
    {nullptr}};

// clang-format off
static PyTypeObject EventObjectType = {
    .ob_base = PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "evp.Event",
    .tp_basicsize = sizeof(EventObject),
    .tp_itemsize = 0,
    .tp_dealloc = (destructor) EventObject_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "An Event (with data)",
    .tp_getset = EventObject_getsetters,
    .tp_new = PyType_GenericNew,
};
// clang-format on

//
// Define the evp module and its methods.
//

static PyMethodDef evp_methods[] = {
    {"on", evp_on, METH_VARARGS, "Schedule a function to run when a matching event occurs."},
    {"collect", evp_collect, METH_VARARGS, "Schedule a function to run collection time points."},
    {"has_definition", evp_has_definition, METH_VARARGS, "Query whether a definition with a given name exists."},
    {"get_parameter",
     evp_get_parameter,
     METH_VARARGS,
     "Get a parameter value, None will be returned if it doesn't exist."},
    {"end_simulation", evp_end_simulation, METH_VARARGS, "Schedule a function to run at the end of simulation."},
    {nullptr, nullptr, 0, nullptr}};

static PyModuleDef evp_module = {
    PyModuleDef_HEAD_INIT, "evp", nullptr, -1, evp_methods, nullptr, nullptr, nullptr, nullptr};

/** Main initialization function.
 *
 *  We need to define the "evp" module, and put the "Event" object class inside it.
 *
 *  @returns The module object
 */
static PyObject* PyInit_evp()
{
    if (PyType_Ready(&EventObjectType) < 0)
        py_err("bad EventObjectType");

    PyObject* mod = PyModule_Create(&evp_module);

    if (!mod)
        py_err("failed to create evp module");

    Py_INCREF(&EventObjectType);

    if (PyModule_AddObject(mod, "Event", (PyObject*)&EventObjectType) < 0) {
        Py_DECREF(&EventObjectType);
        Py_DECREF(mod);
        py_err("failed to add Event object to module");
    }

    enumeration_aliases = PyDict_New();
    if (PyModule_AddObjectRef(mod, "enums", enumeration_aliases) < 0) {
        Py_DECREF(enumeration_aliases);
        Py_DECREF(mod);
        py_err("failed to add enums object to module");
    }

    return mod;
}

PythonPluginHelper::PythonPluginHelper(ProcessorIfc& proc_ifc) : Plugin{proc_ifc}
{
    // initialize the interpreter
    PyImport_AppendInittab("evp", &PyInit_evp);
    Py_Initialize();

    if (PyRun_SimpleString(fmt::format("import sys; sys.path.append(\"{}\"); sys.path.append(\"{}\");",
                                       get_config_path(),
                                       get_pylib_path())
                               .c_str())
        != 0)
    {
        py_err("error appending to sys.path");
    }
}

PythonPluginHelper::~PythonPluginHelper()
{
    for (auto* func : collect_functions)
        Py_DECREF(func);

    for (auto* enum_type : std::views::values(enumerations))
        Py_DECREF(enum_type);

    if (enumeration_aliases) {
        Py_DECREF(enumeration_aliases);
        enumeration_aliases = nullptr;
    }

    // Finalize the interpreter
    Py_FinalizeEx();
}

/** Here we're implementing `on` by adapting between the Python objects and `on` routine
 *  of the C++ plugin API.
 */
void PythonPluginHelper::py_on(const std::string& trigger, PyObject* func)
{
    Action action = [this, trigger, func](Counter*, const event_stream_proto::Event& proto_event) {
        // Create an Event object by calling its constructor.
        // I.e., this is the equivalent of "Event()" in Python.
        PyObject* event_args = Py_BuildValue("()");
        PyObject* event = PyObject_CallObject((PyObject*)&EventObjectType, event_args);
        if (!event)
            py_err("could not create Event object");
        Py_DECREF(event_args);

        // Initialize the Event from protobuf.
        ((EventObject*)event)->from_proto(proto_event, this->proc_ifc);

        // Now we need to call the user's callback with the Event object.
        PyObject* arglist = Py_BuildValue("(O)", event);
        PyObject* result = PyObject_CallObject(func, arglist);
        if (!result)
            py_err(fmt::format("trigger \"{}\" function failed", trigger));
        Py_DECREF(arglist);

        // Finally, we're done with protobuf. If the user hasn't accessed `data`
        // by now, then it will never be generated.
        ((EventObject*)event)->clear_proto();

        Py_XDECREF(result);
        Py_DECREF(event);
    };

    // Finally, the plugin API! This is just the C++ version of what we expressed
    // in Python: call a C++ function (in this case a lambda) when the trigger
    // pattern is encountered.
    //
    proc_ifc.on(this, trigger, action);
}

/** Here we're implementing `has_definition` by adapting between the Python
 * objects and `has_definition` routine of the C++ plugin API.
 */
bool PythonPluginHelper::py_has_definition(const std::string& name)
{
    return proc_ifc.has_definition(name);
}

const Parameter* PythonPluginHelper::py_get_parameter(const std::string& name)
{
    return proc_ifc.has_parameter(name) ? &proc_ifc.get_parameter(name) : nullptr;
}

void PythonPluginHelper::define_value(const Definition& definition)
{
    if (!definition.has_enumeration_id())
        return;

    auto py_enum = enumerations.find(definition.enumeration_id());
    if (py_enum == enumerations.end()) {
        const auto& enum_def = proc_ifc.get_enumeration(definition.enumeration_id());
        py_enum = enumerations.emplace(enum_def.id(), create_enumeration(definition.name(), enum_def)).first;
    }

    if (enumeration_aliases)
        PyDict_SetItemString(enumeration_aliases, definition.name().c_str(), py_enum->second);
}

PyObject* PythonPluginHelper::create_enumeration(const std::string& name, const Enumeration& enum_def)
{
    auto* enum_module = PyImport_ImportModule("enum");
    if (!enum_module)
        return nullptr;

    auto* enum_class = PyObject_GetAttrString(enum_module, "IntEnum");
    auto* global_str = PyObject_GetAttrString(enum_module, "global_str");
    Py_DECREF(enum_module);
    if (!enum_class || !global_str)
        return nullptr;

    auto* enum_dict = PyDict_New();
    for (const auto& [value, name] : enum_def.values())
        PyDict_SetItemString(enum_dict, name.c_str(), PyLong_FromLong(value));

    auto* enum_name = PyUnicode_FromString(name.c_str());
    auto* enumeration = PyObject_CallFunctionObjArgs(enum_class, enum_name, enum_dict, NULL);
    if (PyObject_SetAttrString(enumeration, "__str__", global_str) < 0)
        return nullptr;

    Py_DECREF(enum_name);
    Py_DECREF(enum_class);
    Py_DECREF(enum_dict);

    return enumeration;
}

/** `collect` is a C++ plugin API hook to be called at collection time points.
 */
void PythonPluginHelper::collect(MetricSeries& metrics, uint64_t trigger_time)
{
    PyObject* args = Py_BuildValue("(K)", trigger_time);

    for (auto func : collect_functions) {
        Py_INCREF(func);
        Py_INCREF(args);

        PyObject* result = PyObject_CallObject(func, args);

        if (!result)
            py_err("call to collect function failed");

        // If the collect function returned a dictionary-like object (a mapping),
        // then populate the metric series with the key/value pairs we got.
        //
        if (PyMapping_Check(result)) {
            PyObject* items = PyMapping_Items(result);

            if (!items)
                py_err("could not get items from mapping");

            for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items); ++i) {
                PyObject* kv = PyList_GET_ITEM(items, i);
                PyObject* key = PyTuple_GET_ITEM(kv, 0);
                PyObject* value = PyTuple_GET_ITEM(kv, 1);

                const char* key_str = PyBytes_AsString(PyUnicode_AsASCIIString(key));

                if (PyLong_Check(value)) {
                    int overflow;
                    int64_t i64 = PyLong_AsLongLongAndOverflow(value, &overflow);

                    if (overflow == 0) {
                        metrics[key_str] = i64;
                    } else if (overflow < 0) {
                        py_err("integer underflow");
                    } else if (overflow > 0) {
                        // FIXME: How to catch errors here?
                        uint64_t u64 = PyLong_AsUnsignedLongLong(value);
                        metrics[key_str] = u64;
                    }
                } else if (PyFloat_Check(value)) {
                    metrics[key_str] = PyFloat_AsDouble(value);
                } else if (PyUnicode_Check(value)) {
                    metrics[key_str] = PyBytes_AsString(PyUnicode_AsASCIIString(key));
                } else if (value == Py_None) {
                    metrics[key_str] = NA;
                } else {
                    py_err("unsupported Python object type in collect");
                }
            }

            Py_XDECREF(items);
        }

        Py_XDECREF(result);
        Py_DECREF(args);
        Py_DECREF(func);
    }

    Py_DECREF(args);
}

/** `end_simulation` is a C++ plugin API hook for when we run out of events. So
 *  now we just need to turn around and execute and stored away end_simulation
 *  callbacks that may have been defined.
 */
void PythonPluginHelper::end_simulation()
{
    // I'm using the same empty no_args for all functions and only decref'ing
    // it once at the end. Hope that's kosher...
    PyObject* no_args = Py_BuildValue("()");

    while (!end_simulation_functions.empty()) {
        auto* func = end_simulation_functions.front();
        end_simulation_functions.pop_front();

        PyObject* result = PyObject_CallObject(func, no_args);
        if (!result)
            py_err("end_simulation call failed");
        Py_XDECREF(result);
        Py_DECREF(func);
    }

    Py_DECREF(no_args);
}

/** Build the Python plugin.
 *
 *  You can pass the Python plugin a single Python file to run, optionally with
 *  arguments:
 *
 *      evp ... +python foo.py [args]
 */
Python::Python(ProcessorIfc& proc_ifc, Args& args) : Plugin{proc_ifc}, python_plugin_idx{number_python_plugins++}
{
    // If the interpreter is not already initialized, do so
    if (!python_plugin_helper)
        python_plugin_helper = new PythonPluginHelper(proc_ifc);

    // Our objective in executing Python files is to keep them isolated from each other. That means we have
    // to execute each one with its own global variables. But we can't start with an empty dictionary, or
    // no imports will work. So instead we'll grab the value of __builtins__ from the dictionary of the
    // __main__ module (which is always available) and stuff that into a new dictionary for each file we
    // execute.
    //
    PyObject* main_module = PyImport_ImportModule("__main__"); // new reference
    PyObject* globals = PyModule_GetDict(main_module);         // borrowed reference

    PyObject* __builtins__ = PyUnicode_InternFromString("__builtins__"); // new reference
    PyObject* builtins = PyDict_GetItem(globals, __builtins__);          // borrowed reference

    std::string pyfile;
    if (!args.pop(pyfile))
        throw std::runtime_error{"no python script specified to python plugin"};

    // construct list of arguments to pass to python script
    PyObject* evp_args = PyUnicode_InternFromString("evp_args"); // new reference
    PyObject* arguments = PyList_New(0);                         // new reference
    while (args) {
        std::string argument;
        if (args.pop_any(argument)) {
            PyObject* py_arg = PyUnicode_InternFromString(argument.c_str()); // new reference
            PyList_Append(arguments, py_arg);                                // inc's reference of py_arg
            Py_DECREF(py_arg);
        }
    }
    args.done();

    FILE* fp = fopen(pyfile.c_str(), "rb");
    if (!fp) {
        fp = fopen(fmt::format("{}/{}", get_config_path(), pyfile).c_str(), "rb");

        if (!fp) {
            fp = fopen(fmt::format("{}/plugins/{}", get_config_path(), pyfile).c_str(), "rb");

            if (!fp)
                throw std::runtime_error{fmt::format("could not open file {}", pyfile)};
        }
    }

    // Our fresh globals dictionary will only have __builtins__ and our
    // arguments, but that seems to be enough.
    PyObject* globals_lite = PyDict_New();                // new reference
    PyDict_SetItem(globals_lite, __builtins__, builtins); // inc's __builtins__ and builtins
    PyDict_SetItem(globals_lite, evp_args, arguments);    // inc's evp_args and arguments

    for (const auto& v : proc_ifc.vars())
        PyDict_SetItem(globals_lite, PyUnicode_FromString(v.first.c_str()), PyUnicode_FromString(v.second.c_str()));

    // globals_lite is serving as global and local dictionary here. This seems fishy, but I found
    // some description on the web that indicated it might be necessary to support things like
    // recursive functions.
    //
    // See: https://medium.com/just-me-me-programming-life/python-c-and-symbols-4628fb71a257
    //
    PyObject* file = PyRun_FileEx(fp, pyfile.c_str(), Py_file_input, globals_lite, globals_lite, 1);

    if (!file)
        py_err(fmt::format("error executing {}", pyfile));

    Py_DECREF(evp_args);
    Py_DECREF(arguments);
    Py_DECREF(file);
    Py_DECREF(globals_lite);
    Py_DECREF(main_module);
    Py_DECREF(__builtins__);
}

/** Clear out out global state upon destruction.
 */
Python::~Python()
{
    number_python_plugins--;
    // If we are the last python plugin instance, finalize the interpreter
    if (!number_python_plugins) {
        delete python_plugin_helper;
        python_plugin_helper = nullptr;
    }
}

void Python::define_value(const Definition& definition)
{
    // Only call on helper for first actual plugin, because otherwise we would
    // call any collect hooks registered from python the number of times that
    // there are python plugin instances.
    if (!python_plugin_idx)
        python_plugin_helper->define_value(definition);
}

void Python::collect(MetricSeries& metrics, uint64_t trigger_time)
{
    // Only call on helper for first actual plugin, because otherwise we would
    // call any collect hooks registered from python the number of times that
    // there are python plugin instances.
    if (!python_plugin_idx)
        python_plugin_helper->collect(metrics, trigger_time);
}

void Python::end_simulation()
{
    // Only call on helper for first actual plugin, because otherwise we would
    // call any end_simulation hooks registered from python the number of times
    // that there are python plugin instances.
    if (!python_plugin_idx)
        python_plugin_helper->end_simulation();
}

} // namespace ampere::evp
