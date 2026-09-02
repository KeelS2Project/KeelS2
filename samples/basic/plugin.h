#ifndef KEELS2_SAMPLE_BASIC_PLUGIN_H
#define KEELS2_SAMPLE_BASIC_PLUGIN_H

#include <keels2/keels2.hpp>

class BasicPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 Basic",
        "KeelS2 Project",
        "0.9.0",
        "Minimal native plugin and console-command proof"
    };

    bool Load() override;
    void Unload() override;

private:
    void TestCommand(const CCommandContext& context, const CCommand& command);
};

#endif
