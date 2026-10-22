/*
 * Copyright (c) 2024-2025 Ampere Computing. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <map>

#include <boost/python.hpp>
#include <nlohmann/json.hpp>

#include "accumulator.h"
#include "component.h"
#include "error.h"
#include "link_graph.h"
#include "location.h"
#include "lynx.enum.h"
#include "model.h"
#include "protocols.h"
#include "routing.h"
#include "rules.h"
#include "traffic.h"

namespace ampere::lynx {

using json = nlohmann::json;

void translate(Error const& e)
{
    // Use the Python 'C' API to set up an exception object
    PyErr_SetString(PyExc_RuntimeError, e.what());
}

static boost::python::object traffic_on_link(const Accumulator& self, Channel channel, const boost::python::object& obj)
{
    auto the_map = self.traffic_on_link(
        channel, {boost::python::extract<const Component*>(obj[0]), boost::python::extract<const Component*>(obj[1])});

    PyObject* the_dict = Py_BuildValue("{}");

    // Yikes. Ugly way to get to the enum converter for Opcode. I have to believe there's
    // a better interface in boost::python for this, but I couldn't figure out what it
    // was.
    auto& cv = boost::python::converter::registry::lookup(boost::python::type_id<Opcode>());

    for (const auto& [opcode, traffic] : the_map) {
        Opcode opcode_ = opcode;
        PyObject* key = cv.to_python(reinterpret_cast<void*>(&opcode_));
        PyObject* value = PyFloat_FromDouble(traffic);
        PyDict_SetItem(the_dict, key, value);

        Py_XDECREF(key);
        Py_XDECREF(value);
    }

    boost::python::handle<> handle(the_dict);
    boost::python::object result(handle);

    return result;
}

void dump_accumulated_traffic(const Accumulator& self)
{
    self.dump_accumulated_traffic(std::cout);
};

boost::python::dict accumulated_traffic_as_dict(const Accumulator& self, Channel channel)
{
    boost::python::dict result;

    for (const auto& [link, per_opcode_traffic] : self.traffic_on_channel(channel)) {
        boost::python::dict py_per_opcode_traffic;

        for (const auto& [opcode, traffic] : per_opcode_traffic) {
            py_per_opcode_traffic[opcode] = traffic;
        }

        result[boost::python::make_tuple(boost::ref(link.first), boost::ref(link.second))] = py_per_opcode_traffic;
    }

    return result;
}

class PyRules : public Rules
{
public:
    void add_cross_die_routes(u8 from_die, u8 to_die, const boost::python::list& pyroutes)
    {
        using ListIter = boost::python::stl_input_iterator<boost::python::object>;
        PathList routes;

        for (auto route_it = ListIter(pyroutes); route_it != ListIter(); ++route_it) {
            Path path;

            for (auto comp_it = ListIter(*route_it); comp_it != ListIter(); ++comp_it) {
                path.push_back(boost::python::extract<const Component*>(*comp_it));
            }

            routes.push_back(path);
        }

        this->Rules::add_cross_die_routes(from_die, to_die, routes);
    }

    void add_cross_die_route(u8 from_die, u8 to_die, const boost::python::list& pyroute)
    {
        using ListIter = boost::python::stl_input_iterator<boost::python::object>;
        Path route;

        for (auto comp_it = ListIter(pyroute); comp_it != ListIter(); ++comp_it) {
            route.push_back(boost::python::extract<const Component*>(*comp_it));
        }

        this->Rules::add_cross_die_route(from_die, to_die, route);
    }

    void add_cross_die_route_src_tgt(Channel channel,
                                     const Component* src,
                                     const Component* tgt,
                                     const boost::python::list& pyroute)
    {
        using ListIter = boost::python::stl_input_iterator<boost::python::object>;
        Path route;

        for (auto comp_it = ListIter(pyroute); comp_it != ListIter(); ++comp_it) {
            route.push_back(boost::python::extract<const Component*>(*comp_it));
        }

        this->Rules::add_cross_die_route_src_tgt(channel, src, tgt, route);
    }

    boost::python::list cross_die_routes(u8 from_die, u8 to_die)
    {
        boost::python::list result;
        const auto& routes = this->Rules::cross_die_routes(from_die, to_die);

        for (const auto& route : routes) {
            boost::python::list path;

            for (const auto& comp : route) {
                path.append(boost::ref(comp));
            }

            result.append(path);
        }

        return result;
    }

    boost::python::list cross_die_routes_src_tgt(Channel channel, const Component* src, const Component* tgt)
    {
        boost::python::list result;
        const auto& routes = this->Rules::cross_die_routes_src_tgt(channel, src, tgt);

        for (const auto& route : routes) {
            boost::python::list path;

            for (const auto& comp : route) {
                path.append(boost::ref(comp));
            }

            result.append(path);
        }

        return result;
    }

    boost::python::list py_get_targets_by_channel_and_type(Channel channel, ComponentType comp_type) const
    {
        boost::python::list py_targets;
        const auto& targets = get_targets_by_channel_and_type(channel, comp_type);

        for (const auto& tgt : targets) {
            py_targets.append(boost::python::make_tuple(tgt.first, tgt.second));
        }

        return py_targets;
    }

    void py_set_targets_by_channel_and_type(Channel channel,
                                            ComponentType comp_type,
                                            const boost::python::list& py_targets)
    {
        using ListIter = boost::python::stl_input_iterator<boost::python::object>;
        std::vector<std::pair<ComponentType, Scope>> targets;

        for (auto tgt_it = ListIter(py_targets); tgt_it != ListIter(); ++tgt_it) {
            ComponentType tgt_comp_type = boost::python::extract<ComponentType>(PyTuple_GetItem(tgt_it->ptr(), 0));
            Scope tgt_scope = boost::python::extract<Scope>(PyTuple_GetItem(tgt_it->ptr(), 1));

            targets.emplace_back(tgt_comp_type, tgt_scope);
        }

        set_targets_by_channel_and_type(channel, comp_type, targets);
    }

    boost::python::str dump_json_rules()
    {
        std::string s = json(*this).dump(4);
        return boost::python::str(s);
    }
};

boost::python::list find_cycle(LinkGraph& self)
{
    boost::python::list result;

    for (const auto& comp : self.find_cycle()) {
        result.append(boost::ref(comp));
    }

    return result;
}

boost::python::list find_cycle_with_contributing_paths(LinkGraph& self)
{
    auto paths = self.find_cycle_with_contributing_paths();
    boost::python::list result;

    for (const auto& path : paths) {
        boost::python::list py_path;

        for (const auto& comp : path)
            py_path.append(boost::ref(comp));

        result.append(py_path);
    }

    return result;
}

class PyModel : public ModelBase
{
public:
    using ModelBase::ModelBase;

    void add_component(boost::python::object obj)
    {
        const Component* comp = boost::python::extract<const Component*>(obj);
        py_objs[comp] = obj;
        this->ModelBase::add_component(comp);
    }

    boost::python::object find_component(Location loc) const
    {
        const Component* comp = this->ModelBase::find_component(loc);
        return py_objs.at(comp);
    }

    boost::python::object find_rtr(Location loc) const
    {
        const Component* comp = this->ModelBase::find_rtr(loc);
        return py_objs.at(comp);
    }

    boost::python::list components_by_type(ComponentType type) const
    {
        boost::python::list result;

        for (const auto comp : this->ModelBase::components_by_type(type))
            result.append(py_objs.at(comp));

        return result;
    }

    boost::python::list all_connections() const
    {
        boost::python::list result;

        for (const auto& conn : this->ModelBase::all_connections())
            result.append(boost::python::make_tuple(boost::ref(conn.first), boost::ref(conn.second)));

        return result;
    }

    boost::python::list all_targets(const PyRules& rules, const Component* src, Channel chan) const
    {
        boost::python::list result;

        for (const auto& [comp_type, scope] : rules.get_targets_by_channel_and_type(chan, src->type())) {
            for (const auto& tgt : get_targets(*this, comp_type, scope, src->die_id())) {
                if (rules.allowed(chan, src, tgt))
                    result.append(boost::ref(tgt));
            }
        }

        return result;
    }

    boost::python::str dump_json() const
    {
        std::string s = json(*this).dump(4);
        return boost::python::str(s);
    }

private:
    std::unordered_map<const Component*, boost::python::object> py_objs;
};

namespace agent_funcs {

void ReadShared(const Component& self,
                const PyModel& model,
                const PyRules& rules,
                float traffic,
                bool l3_hit,
                Accumulator& accumulator)
{
    if (self.type() == ComponentType::Core) {
        if (l3_hit)
            protocols::ReadShared_L3Hit(Traffic{model, rules, accumulator, traffic}, &self);
        else
            protocols::ReadShared_L3Miss(Traffic{model, rules, accumulator, traffic}, &self);
    } else {
        throw NotImplementedByComponent{"ReadShared is not implemented in the {} component", self.type()};
    }
}

void ReadShared_CXL(const Component& self,
                    const Component& cxl,
                    const PyModel& model,
                    const PyRules& rules,
                    float traffic,
                    Accumulator& accumulator)
{
    if (self.type() == ComponentType::Core && cxl.type() == ComponentType::CXL) {
        protocols::ReadShared_CXL(Traffic{model, rules, accumulator, traffic}, &self, &cxl);
    } else {
        throw NotImplementedByComponent{
            "ReadShared_CXL is not implemented for components {} -> {}", self.type(), cxl.type()};
    }
}

void ReadUnique(const Component& self,
                const PyModel& model,
                const PyRules& rules,
                float traffic,
                bool l3_hit,
                Accumulator& accumulator)
{
    if (self.type() == ComponentType::Core) {
        if (l3_hit)
            protocols::ReadUnique_L3Hit(Traffic{model, rules, accumulator, traffic}, &self);
        else
            protocols::ReadUnique_L3Miss(Traffic{model, rules, accumulator, traffic}, &self);
    } else {
        throw NotImplementedByComponent{"ReadUnique is not implemented in the {} component", self.type()};
    }
}

void ReadUnique_CXL(const Component& self,
                    const Component& cxl,
                    const PyModel& model,
                    const PyRules& rules,
                    float traffic,
                    Accumulator& accumulator)
{
    if (self.type() == ComponentType::Core && cxl.type() == ComponentType::CXL) {
        protocols::ReadUnique_CXL(Traffic{model, rules, accumulator, traffic}, &self, &cxl);
    } else {
        throw NotImplementedByComponent{
            "ReadUnique_CXL is not implemented for component types {} -> {}", self.type(), cxl.type()};
    }
}

void ReadOnce(
    const Component& self, const PyModel& model, const PyRules& rules, float traffic, Accumulator& accumulator)
{
    if (self.type() == ComponentType::IOR || self.type() == ComponentType::IORr) {
        protocols::ReadOnce(Traffic{model, rules, accumulator, traffic}, &self);
    } else {
        throw NotImplementedByComponent{"ReadOnce is not implemented in the {} component", self.type()};
    }
}

void MakeUnique(
    const Component& self, const PyModel& model, const PyRules& rules, float traffic, Accumulator& accumulator)
{
    if (self.type() == ComponentType::Core)
        protocols::MakeUnique_L3Miss(Traffic{model, rules, accumulator, traffic}, &self);
    else
        throw NotImplementedByComponent{"MakeUnique is not implemented in the {} component", self.type()};
}

void WriteBackFull(const Component& self,
                   const PyModel& model,
                   const PyRules& rules,
                   float traffic,
                   bool l3_evict,
                   Accumulator& accumulator)
{
    if (self.type() == ComponentType::Core) {
        if (l3_evict)
            protocols::WriteBackFull_FromL3(Traffic{model, rules, accumulator, traffic}, &self);
        else
            protocols::WriteBackFull(Traffic{model, rules, accumulator, traffic}, &self);
    } else {
        throw NotImplementedByComponent{"WriteBackFull is not implemented in the {} component", self.type()};
    }
}

void WriteEvictFull(
    const Component& self, const PyModel& model, const PyRules& rules, float traffic, Accumulator& accumulator)
{
    if (self.type() == ComponentType::Core)
        protocols::WriteEvictFull(Traffic{model, rules, accumulator, traffic}, &self);
    else
        throw NotImplementedByComponent{"WriteEvictFull is not implemented in the {} component", self.type()};
}

void WriteNoSnpPtl(
    const Component& self, const PyModel& model, const PyRules& rules, float traffic, Accumulator& accumulator)
{
    if (self.type() == ComponentType::IOR || self.type() == ComponentType::IORw) {
        protocols::WriteNoSnpPtl(Traffic{model, rules, accumulator, traffic}, &self);
    } else {
        throw NotImplementedByComponent{"WriteNoSnpPtl is not implemented in the {} component", self.type()};
    }
}

void Evict(const Component& self, const PyModel& model, const PyRules& rules, float traffic, Accumulator& accumulator)
{
    if (self.type() == ComponentType::Core)
        protocols::Evict(Traffic{model, rules, accumulator, traffic}, &self);
    else
        throw NotImplementedByComponent{"Evict is not implemented in the {} component", self.type()};
}

void ReadNoSnp_P2P(const Component& ior,
                   const Component& ioh,
                   const PyModel& model,
                   const PyRules& rules,
                   float traffic,
                   Accumulator& accumulator)
{
    if ((ior.type() == ComponentType::IOR || ior.type() == ComponentType::IORr)
        && ((ioh.type() == ComponentType::IOH || ioh.type() == ComponentType::IOHr)))
    {
        protocols::ReadNoSnp_P2P(Traffic{model, rules, accumulator, traffic}, &ior, &ioh);
    } else {
        throw NotImplementedByComponent{
            "ReadNoSnp_P2P is not implemented for component types {} -> {}", ior.type(), ioh.type()};
    }
}

void WriteNoSnp_P2P(const Component& ior,
                    const Component& ioh,
                    const PyModel& model,
                    const PyRules& rules,
                    float traffic,
                    Accumulator& accumulator)
{
    if ((ior.type() == ComponentType::IOR || ior.type() == ComponentType::IORw)
        && ((ioh.type() == ComponentType::IOH || ioh.type() == ComponentType::IOHw)))
    {
        protocols::WriteNoSnp_P2P(Traffic{model, rules, accumulator, traffic}, &ior, &ioh);
    } else {
        throw NotImplementedByComponent{
            "WriteNoSnp_P2P is not implemented for component types {} -> {}", ior.type(), ioh.type()};
    }
}

} // namespace agent_funcs

LinkGraph build_link_graph_wrap(const PyModel& model, const PyRules& rules, Channel channel)
{
    return build_link_graph(model, rules, channel);
}

std::string component_repr(const Component& comp)
{
    return fmt::format("{}", comp);
}

boost::python::list paths_to_target_wrap(
    const PyModel& model, const Component* src, const Component* tgt, Channel channel, const PyRules& rules)
{
    boost::python::list py_pathlist;

    for (const auto& path : paths_to_target(model, rules, src, tgt, channel)) {
        boost::python::list py_path;

        for (const auto& comp : path)
            py_path.append(boost::ref(comp));

        py_pathlist.append(py_path);
    }

    return py_pathlist;
}

boost::python::str dump_json_graph(const LinkGraph& graph)
{
    std::string s = json{graph}.dump(4);
    return boost::python::str(s);
}

} // namespace ampere::lynx

BOOST_PYTHON_MODULE(lynx)
{
    using namespace ampere::lynx;
    using namespace boost::python;

    register_exception_translator<NoComponent>(&translate);
    register_exception_translator<RuleError>(&translate);
    register_exception_translator<DuplicateLocation>(&translate);
    register_exception_translator<NotImplementedByComponent>(&translate);
    register_exception_translator<BadConnectivity>(&translate);
    register_exception_translator<BadGraph>(&translate);
    register_exception_translator<NoRoute>(&translate);

    enum_<Channel>("Channel")
        .value("REQ", Channel::REQ)
        .value("RSP0", Channel::RSP0)
        .value("RSP1", Channel::RSP1)
        .value("SNP", Channel::SNP)
        .value("DAT", Channel::DAT);

    enum_<ComponentType>("ComponentType")
        .value("Generic", ComponentType::Generic)
        .value("RTR", ComponentType::RTR)
        .value("Core", ComponentType::Core)
        .value("PxHA", ComponentType::PxHA)
        .value("PmHA", ComponentType::PmHA)
        .value("MCU", ComponentType::MCU)
        .value("ADI", ComponentType::ADI)
        .value("IOR", ComponentType::IOR)
        .value("IOH", ComponentType::IOH)
        .value("CCL_ACR_SYS", ComponentType::CCL_ACR_SYS)
        .value("CCL_ACR_GIC", ComponentType::CCL_ACR_GIC)
        .value("CCL_ACR_CTRACE", ComponentType::CCL_ACR_CTRACE)
        .value("CCL_ACR_MTRACE", ComponentType::CCL_ACR_MTRACE)
        .value("CCL_ACR_DDR", ComponentType::CCL_ACR_DDR)
        .value("IOD_ACR_SYS", ComponentType::IOD_ACR_SYS)
        .value("IOD_ACR_GIC", ComponentType::IOD_ACR_GIC)
        .value("IOD_ACR_MTRACE", ComponentType::IOD_ACR_MTRACE)
        .value("IOD_ACR_SMMU", ComponentType::IOD_ACR_SMMU)
        .value("NHP", ComponentType::NHP)
        .value("MHA_SYS", ComponentType::MHA_SYS)
        .value("MHA_GIC", ComponentType::MHA_GIC)
        .value("MHA_DVM", ComponentType::MHA_DVM)
        .value("CXL", ComponentType::CXL)
        .value("COM", ComponentType::COM)
        .value("IORr", ComponentType::IORr)
        .value("IORw", ComponentType::IORw)
        .value("IOHr", ComponentType::IOHr)
        .value("IOHw", ComponentType::IOHw);

    enum_<Opcode>("Opcode")
        .value("ReadShared", Opcode::ReadShared)
        .value("ReadUnique", Opcode::ReadUnique)
        .value("ReadOnce", Opcode::ReadOnce)
        .value("MakeUnique", Opcode::MakeUnique)
        .value("Evict", Opcode::Evict)
        .value("WriteEvictFull", Opcode::WriteEvictFull)
        .value("WriteBackFull", Opcode::WriteBackFull)
        .value("ReadNoSnp", Opcode::ReadNoSnp)
        .value("ReadNoSnpSep", Opcode::ReadNoSnpSep)
        .value("SnpSharedFwd", Opcode::SnpSharedFwd)
        .value("SnpUnique", Opcode::SnpUnique)
        .value("SnpUniqueFwd", Opcode::SnpUniqueFwd)
        .value("SnpStashShared", Opcode::SnpStashShared)
        .value("SnpStashUnique", Opcode::SnpStashUnique)
        .value("RespSepData", Opcode::RespSepData)
        .value("Comp", Opcode::Comp)
        .value("CompAck", Opcode::CompAck)
        .value("CompDBID", Opcode::CompDBID)
        .value("SnpResp", Opcode::SnpResp)
        .value("SnpRespFwd", Opcode::SnpRespFwd)
        .value("ReadReceipt", Opcode::ReadReceipt)
        .value("DataSepResp", Opcode::DataSepResp)
        .value("CompData", Opcode::CompData)
        .value("CopyBackWrData", Opcode::CopyBackWrData)
        .value("NonCopyBackWrData", Opcode::NonCopyBackWrData)
        .value("WriteNoSnpFull", Opcode::WriteNoSnpFull)
        .value("WriteNoSnpPtl", Opcode::WriteNoSnpPtl)
        .value("WriteUniquePtl", Opcode::WriteUniquePtl)
        .value("DBIDResp", Opcode::DBIDResp);

    enum_<Scope>("Scope").value("SameDie", Scope::SameDie).value("CrossDie", Scope::CrossDie);

    class_<Location>("Location", init<int, int, int, optional<int, int>>())
        .def_readwrite("die_id", &Location::die_id)
        .def_readwrite("x", &Location::x)
        .def_readwrite("y", &Location::y)
        .def_readwrite("port", &Location::port)
        .def_readwrite("didx", &Location::didx)
        .def("__eq__", &Location::operator==)
        .def("__ne__", &Location::operator!=);

    class_<Component, boost::noncopyable>("Component", init<ComponentType, Location>())
        .add_property("die_id", &Component::die_id)
        .add_property("x", &Component::x)
        .add_property("y", &Component::y)
        .add_property("port", &Component::port)
        .add_property("didx", &Component::didx)
        .add_property("type", &Component::type)
        .add_property("loc", &Component::loc)
        .def("ReadShared", agent_funcs::ReadShared)
        .def("ReadUnique", agent_funcs::ReadUnique)
        .def("ReadOnce", agent_funcs::ReadOnce)
        .def("MakeUnique", agent_funcs::MakeUnique)
        .def("WriteBackFull", agent_funcs::WriteBackFull)
        .def("WriteEvictFull", agent_funcs::WriteEvictFull)
        .def("WriteNoSnpPtl", agent_funcs::WriteNoSnpPtl)
        .def("Evict", agent_funcs::Evict)
        .def("ReadNoSnp_P2P", agent_funcs::ReadNoSnp_P2P)
        .def("WriteNoSnp_P2P", agent_funcs::WriteNoSnp_P2P)
        .def("ReadShared_CXL", agent_funcs::ReadShared_CXL)
        .def("ReadUnique_CXL", agent_funcs::ReadUnique_CXL)
        .def("__repr__", component_repr);

    void (Rules::*allow_all_on_all_channels)() = &PyRules::allow_all;
    void (Rules::*allow_all_on_one_channel)(Channel) = &PyRules::allow_all;
    void (Rules::*allow_on_all_channels)(const Component*, const Component*) = &PyRules::allow;
    void (Rules::*allow_on_one_channel)(Channel, const Component*, const Component*) = &PyRules::allow;
    void (Rules::*disallow_on_all_channels)(const Component*, const Component*) = &PyRules::disallow;
    void (Rules::*disallow_on_one_channel)(Channel, const Component*, const Component*) = &PyRules::disallow;
    void (Rules::*add_xy_override)(const Component*, const Component*) = &PyRules::add_xy_override;
    void (Rules::*add_xy_override_chan)(const Component*, const Component*, Channel) = &PyRules::add_xy_override;

    class_<PyRules>("Rules", init<>())
        .def("xfirst", &PyRules::xfirst)
        .def("allowed", &PyRules::allowed)
        .def("set_xfirst", &PyRules::set_xfirst)
        .def("set_yfirst", &PyRules::set_yfirst)
        .def("allow_all", allow_all_on_all_channels)
        .def("allow_all", allow_all_on_one_channel)
        .def("allow", allow_on_all_channels)
        .def("allow", allow_on_one_channel)
        .def("disallow", disallow_on_all_channels)
        .def("disallow", disallow_on_one_channel)
        .def("add_cross_die_route", &PyRules::add_cross_die_route)
        .def("add_cross_die_routes", &PyRules::add_cross_die_routes)
        .def("add_cross_die_route_src_tgt", &PyRules::add_cross_die_route_src_tgt)
        .def("cross_die_routes", &PyRules::cross_die_routes)
        .def("cross_die_routes_src_tgt", &PyRules::cross_die_routes_src_tgt)
        .def("add_xy_override", add_xy_override)
        .def("add_xy_override_chan", add_xy_override_chan)
        .def("channel_map", &PyRules::channel_map)
        .def("set_channel_map", &PyRules::set_channel_map)
        .def("get_targets_by_channel_and_type", &PyRules::py_get_targets_by_channel_and_type)
        .def("set_targets_by_channel_and_type", &PyRules::py_set_targets_by_channel_and_type)
        .def("dmt_allow", &PyRules::dmt_allow)
        .def("dmt_disallow", &PyRules::dmt_disallow)
        .def("disconnect", &PyRules::disconnect)
        .def("dump_json", &PyRules::dump_json_rules);

    class_<Accumulator>("Accumulator", init<>())
        .def("traffic_on_link", traffic_on_link)
        .def("dump_accumulated_traffic", dump_accumulated_traffic)
        .def("as_dict", accumulated_traffic_as_dict);

    class_<PyModel>("Model", init<>())
        .def("add_component", &PyModel::add_component)
        .def("find_component", &PyModel::find_component)
        .def("find_rtr", &PyModel::find_rtr)
        .def("components_by_type", &PyModel::components_by_type)
        .def("connect_components", &PyModel::connect_components)
        .def("connect_components_one_way", &PyModel::connect_components_one_way)
        .def("all_connections", &PyModel::all_connections)
        .def("all_targets", &PyModel::all_targets)
        .def("dump_json", &PyModel::dump_json);

    class_<LinkGraph>("LinkGraph", init<>())
        .def("has_cycle", &LinkGraph::has_cycle)
        .def("find_cycle", find_cycle)
        .def("find_cycle_with_contributing_paths", find_cycle_with_contributing_paths)
        .def("dump_json", dump_json_graph);

    def("build_link_graph", build_link_graph_wrap);
    def("paths_to_target", paths_to_target_wrap);
}
