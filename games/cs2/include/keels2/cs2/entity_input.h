#ifndef KEELS2_CS2_ENTITY_INPUT_H
#define KEELS2_CS2_ENTITY_INPUT_H
#include <keels2/cs2/native_bridge.h>
#include <keels2/platform/loaded_module.h>
namespace keels2::cs2 {
KeelResult ResolveEntityInput(const platform::LoadedModule& module, const std::string& profile,
    KeelCs2EntityInputBindings& bindings, std::string& error);
}
#endif
