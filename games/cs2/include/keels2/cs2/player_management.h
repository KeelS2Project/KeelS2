#ifndef KEELS2_CS2_PLAYER_MANAGEMENT_H
#define KEELS2_CS2_PLAYER_MANAGEMENT_H

#include <keels2/cs2/native_bridge.h>
#include <keels2/platform/loaded_module.h>

namespace keels2::cs2
{
KeelResult ResolvePlayerManagement(const platform::LoadedModule& module,
    const std::string& profile, KeelCs2PlayerManagementBindings& bindings, std::string& error);
}
#endif
