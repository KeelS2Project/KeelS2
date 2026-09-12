#include <keels2/authoring.hpp>

using namespace keels2::authoring;

class Example : public Plugin
{
public:
    static constexpr PluginInfo Info{
        .name = "",
        .author = "KeelS2",
        .version = "1.1.0",
        .description = "Compile diagnostic"
    };
};

KEELS2_PLUGIN(Example)
