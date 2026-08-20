#ifndef KEELS2_SAMPLE_CONVAR_PLUGIN_H
#define KEELS2_SAMPLE_CONVAR_PLUGIN_H

#include <keels2/keels2.hpp>

#include <cstdint>
class ConVarPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "KeelS2 ConVar Sample",
        "KeelS2 Project",
        "0.5.0",
        "Typed brokered ConVars with owned change callbacks"
    };

    bool Load() override;
    void Unload() override;

private:
    void SetInteger(const CCommandContext& context, const CCommand& command);
    void IntegerChanged(
        CConVarRef<std::int32_t>* convar,
        CSplitScreenSlot slot,
        const std::int32_t* new_value,
        const std::int32_t* old_value);

    CConVarRef<std::int32_t>* integer_{};
    CConVarRef<bool>* boolean_{};
    CConVarRef<float>* floating_{};
    CConVarRef<CUtlString>* string_{};
};

#endif
