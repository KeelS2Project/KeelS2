#ifndef KEELS2_CS2_ENTITY_TOOLS_H
#define KEELS2_CS2_ENTITY_TOOLS_H
#include <keels2/cs2/native_bridge.h>
#include <keels2/platform/loaded_module.h>
namespace keels2::cs2 {
KeelResult ResolveEntityTools(const platform::LoadedModule& module, const std::string& profile,
    KeelCs2EntityToolBindings& bindings, std::string& error);
KeelResult ResolveEntityToolClass(const platform::LoadedModule& module, const char* name,
    const KeelCs2EntityToolBindings& bindings, KeelCs2EntityToolClass& target, std::string& error);
}
#endif
