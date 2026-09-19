#include <keels2/authoring.hpp>

namespace docs
{
using namespace keels2::authoring;

class FirstPlugin final : public Plugin
{
public:
    static constexpr PluginInfo Info{
        .name = "Docs First Plugin",
        .author = "KeelS2 documentation",
        .version = "1.0.0",
        .description = "A complete minimal native plugin"
    };
};
}

KEELS2_PLUGIN(docs::FirstPlugin)
