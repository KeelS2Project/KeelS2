#ifndef KEELS2_CS2_ENTITY_OUTPUT_H
#define KEELS2_CS2_ENTITY_OUTPUT_H
#include <keels2/plugin.h>
#include <keels2/platform/loaded_module.h>
namespace keels2::cs2 {
using EntityOutputFunction = void (*)(void* output, void* activator, void* caller,
    void* variant, float delay, void* context, void* connections);

KeelResult ResolveEntityOutput(const platform::LoadedModule& module, const std::string& profile,
    void*& function, std::string& error);
}
#endif
