#ifndef KEELS2_STUB_PLUGIN_H
#define KEELS2_STUB_PLUGIN_H

#include <keels2/authoring.hpp>

namespace stub
{
using namespace keels2::authoring;

class StubPlugin final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        .name = "KeelS2 Stub",
        .author = "KeelS2 Project",
        .version = "1.0.0",
        .description = "Minimal KeelS2 plugin"
    };
};
}

#endif
