#ifndef KEELS2_CS2_ROUND_CONTROL_H
#define KEELS2_CS2_ROUND_CONTROL_H
#include <keels2/cs2/native_bridge.h>
#include <keels2/platform/loaded_module.h>
namespace keels2::cs2
{
KeelResult ResolveRoundControl(const platform::LoadedModule& module,
    const std::string& profile, KeelCs2RoundBindings& bindings, std::string& error);
}
#endif
