#include "dsv.hpp"
#include "ECS.hpp"
#include <fstream>
#include <cxxabi.h>

void dsviz_show(ecs::IComponentBuffer *P, DSViz::IViz &viz);
typedef DSViz::Mock<ecs::IComponentBuffer, dsviz_show> mock_icb;

void dsviz_show(ecs::IComponentManager *P, DSViz::IViz &viz);
typedef DSViz::Mock<ecs::IComponentManager, dsviz_show> mock_icm;

void dsviz_show(ecs::IComponentBuffer *P, DSViz::IViz &viz)
{
    DSViz::TableNode node(viz);
    viz.setName(mock_icb::get(P), node.name);

    int status;
    char *demangledName = abi::__cxa_demangle(P->getType().name(), nullptr, nullptr, &status);
    if (status == 0)
    {
        node.add("name", std::string(demangledName));
        free(demangledName);
    }

    node.add("size", P->size());
    if (P->manager)
        node.addPointer("manager", mock_icm::get(P->manager));
    if (P->parent)
        node.addPointer("parent", mock_icb::get(P->parent));
    if (P->children) {
        std::vector<DSViz::IDataStructure *> children;
        for (auto* Head = P->children; Head != nullptr; Head = Head->next)
            children.push_back(mock_icb::get(Head));
        node.addChildren("children", children.data(), children.size());
    }
}

void dsviz_show(ecs::IComponentManager *P, DSViz::IViz &viz)
{
    DSViz::TableNode node(viz);
    viz.setName(mock_icm::get(P), node.name);

    int status;
    char *demangledName = abi::__cxa_demangle(P->getType().name(), nullptr, nullptr, &status);
    if (status == 0)
    {
        node.add("type", std::string(demangledName));
        free(demangledName);
    }

    if (P->parent)
        node.addPointer("parent", mock_icm::get(P->parent));
    if (P->registy)
        node.addPointer("registy", mock_icb::get(P->registy));

    for (auto &[key, value] : P->components)
    {
        int status;
        char *demangledName = abi::__cxa_demangle(key.name(), nullptr, nullptr, &status);
        if (status == 0)
        {
            node.addPointer(demangledName, mock_icb::get(value));
            free(demangledName);
        }
    }
}

void dump(ecs::IComponentManager *icm, std::string name)
{
    DSViz::Dot dot;
    dot.load_ds(mock_icm::get(icm));

    std::ofstream file(name, std::ios::out);
    file << dot.print();
}