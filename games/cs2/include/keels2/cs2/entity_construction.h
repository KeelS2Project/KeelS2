#ifndef KEELS2_CS2_ENTITY_CONSTRUCTION_H
#define KEELS2_CS2_ENTITY_CONSTRUCTION_H
#include <keels2/cs2/native_bridge.h>
#include <keels2/platform/loaded_module.h>
#include <string>
namespace keels2::cs2 {
KeelResult ResolveEntityConstruction(const platform::LoadedModule& module, const std::string& profile,
    KeelCs2EntityConstructionBindings& bindings, std::string& error);
}
#endif
