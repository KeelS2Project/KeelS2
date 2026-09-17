#ifndef KEELS2_CS2_ENTITY_WRITES_H
#define KEELS2_CS2_ENTITY_WRITES_H
#include <keels2/platform/loaded_module.h>
#include <keels2/plugin.h>

namespace keels2::cs2
{
KeelResult ResolveEntityWrites(const platform::LoadedModule& module,
    const std::string& profile, void*& notify, std::string& error);
}
#endif
