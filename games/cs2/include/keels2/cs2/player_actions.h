#pragma once

#include <keels2/cs2/native_bridge.h>
#include <keels2/platform/loaded_module.h>

namespace keels2::cs2
{
KeelResult ResolvePlayerActions(const platform::LoadedModule& module,
    const std::string& profile, KeelCs2PlayerActionBindings& bindings, std::string& error);
}
