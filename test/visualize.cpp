#include "dsv.hpp"
#include "ECS.hpp"
#include <fstream>
#include <string>
#include <typeinfo>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

namespace
{
std::string demangle_name(const char *mangled)
{
#if defined(__GNUC__) || defined(__clang__)
  int status = 0;
  char *demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
  if (status == 0 && demangled != nullptr)
  {
    std::string out(demangled);
    free(demangled);
    return out;
  }
#endif
  return mangled ? std::string(mangled) : std::string();
}
} // namespace

void dsviz_show(ecs::IComponentBuffer *P, DSViz::IViz &viz);
typedef DSViz::Mock<ecs::IComponentBuffer, dsviz_show> mock_icb;

void dsviz_show(ecs::IComponentManager *P, DSViz::IViz &viz);
typedef DSViz::Mock<ecs::IComponentManager, dsviz_show> mock_icm;

void dsviz_show(ecs::IComponentBuffer *P, DSViz::IViz &viz)
{
    DSViz::TableNode node(viz);
    viz.setName(mock_icb::get(P), node.name);

    node.add("name", demangle_name(P->getType().name()));

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

    node.add("type", demangle_name(P->getType().name()));

    if (P->parent)
        node.addPointer("parent", mock_icm::get(P->parent));
    if (P->registy)
        node.addPointer("registy", mock_icb::get(P->registy));

    for (auto &[key, value] : P->components)
    {
        node.addPointer(demangle_name(key.name()), mock_icb::get(value));
    }
}

void dump(ecs::IComponentManager *icm, std::string name)
{
    DSViz::Dot dot;
    dot.load_ds(mock_icm::get(icm));

    std::ofstream file(name, std::ios::out);
    file << dot.print();
}
