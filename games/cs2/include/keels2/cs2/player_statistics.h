#ifndef KEELS2_CS2_PLAYER_STATISTICS_H
#define KEELS2_CS2_PLAYER_STATISTICS_H
#include <keels2/cs2/native_bridge.h>
#include <keels2/platform/loaded_module.h>
namespace keels2::cs2
{
KeelResult ResolvePlayerStatistics(const platform::LoadedModule& module, const std::string& profile,
    KeelCs2PlayerStatisticsBindings& bindings, std::string& error);
}
#endif
