#include <keels2/keels2.hpp>

class StubPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Stub",
        "KeelS2 Project",
        "1.0.0",
        "Minimal KeelS2 plugin"
    };

    bool Load() override
    {
        return true;
    }

    void Unload() override
    {
    }
};

KEELS2_PLUGIN(StubPlugin)
