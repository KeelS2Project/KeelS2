#include "plugin.h"

#include <charconv>
#include <string>

bool ConVarPlugin::Load()
{
    integer_ = CreateConVar<std::int32_t>(
            "keels2_sample_int",
            FCVAR_NOTIFY,
            "KeelS2 sample bounded integer",
            std::int32_t{7},
            true,
            std::int32_t{1},
            true,
            std::int32_t{20},
            &ConVarPlugin::IntegerChanged);
    boolean_ = CreateConVar<bool>(
            "keels2_sample_bool",
            FCVAR_NONE,
            "KeelS2 sample boolean",
            true);
    floating_ = CreateConVar<float>(
            "keels2_sample_float",
            FCVAR_NONE,
            "KeelS2 sample bounded float",
            1.5F,
            true,
            0.5F,
            true,
            2.5F);
    string_ = CreateConVar<CUtlString>(
            "keels2_sample_string",
            FCVAR_NONE,
            "KeelS2 sample string",
            CUtlString("keels2"));
    if (!integer_ || !boolean_ || !floating_ || !string_ || !CreateCommand(
            "keel_cvar_sample",
            "Reads or sets the KeelS2 sample integer",
            &ConVarPlugin::SetInteger))
    {
        return false;
    }

    const std::string message =
        "typed values ready: int=" + std::to_string(integer_->Get()) +
        " bool=" + (boolean_->Get() ? "true" : "false") +
        " float=" + std::to_string(floating_->Get()) +
        " string=" + string_->Get().Get();
    LogMessage(message.c_str());
    return true;
}

void ConVarPlugin::Unload()
{
    LogMessage("unloaded after brokered ConVar cleanup");
}

void ConVarPlugin::SetInteger(const CCommandContext& context, const CCommand& command)
{
    static_cast<void>(context);
    if (command.ArgC() == 1)
    {
        const std::string message =
            "current keels2_sample_int=" + std::to_string(integer_->Get());
        LogMessage(message.c_str());
        return;
    }
    if (command.ArgC() != 2)
    {
        LogError("usage: keel_cvar_sample [integer]");
        return;
    }
    std::int32_t value{};
    const char* begin = command[1];
    const char* end = begin + std::char_traits<char>::length(begin);
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
    {
        LogError("could not set keels2_sample_int");
        return;
    }
    integer_->Set(value);
    const std::string message = "set keels2_sample_int=" + std::to_string(integer_->Get());
    LogMessage(message.c_str());
}

void ConVarPlugin::IntegerChanged(
    CConVarRef<std::int32_t>* convar,
    CSplitScreenSlot slot,
    const std::int32_t* new_value,
    const std::int32_t* old_value)
{
    static_cast<void>(slot);
    if (!convar || !new_value || !old_value)
    {
        return;
    }
    const std::string message =
        std::string(convar->GetName()) + " changed from " + std::to_string(*old_value) +
        " to " + std::to_string(*new_value);
    LogMessage(message.c_str());
}

KEELS2_PLUGIN(ConVarPlugin)
