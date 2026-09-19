#include <keels2/authoring.hpp>
#include <cstdint>
#include <string_view>

extern "C" std::int32_t DocsFormat(const char* format, ...);
extern "C" const char* DocsFormattedText();

namespace docs
{
using namespace keels2::authoring;

class HookFormat final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Hook Format", "KeelS2 documentation", "1.0.0",
        "Observe and replace formatted text from a native variadic function"};

    bool Load() override
    {
        return hooks.Connect(HostContext()) == KEEL_RESULT_OK
            && hooks.ResolveVafmt<std::int32_t()>(
                keels2::kh::TargetSpec::Address(reinterpret_cast<void*>(&DocsFormat)), target) == KEEL_RESULT_OK
            && hooks.AddCallback<&HookFormat::Text>(target, callback, KH_PHASE_PRE, 0, *this) == KEEL_RESULT_OK
            && CreateCommand("keel_docs_hook_format", "Run the owned formatted-text target", &HookFormat::Run);
    }

private:
    Action Text(keels2::kh::Frame& frame)
    {
        const auto text = frame.Argument<const char*>(0);

        if (!text || !*text)
            return PLUGIN_CONTINUE;

        if (std::string_view(*text) == "Score 7, ratio 2.5")
        {
            LogMessage("Formatted input: {}", *text);
            const char* replacement = "Hook kept 100% and %s literal";

            if (!frame.SetArgument(0, replacement))
                LogWarning("Formatted text replacement failed.");
        }

        return PLUGIN_CONTINUE;
    }

    void Run(const CCommandContext&, const CCommand&)
    {
        const auto size = DocsFormat("Score %d, ratio %.1f", 7, 2.5);
        LogMessage("DocsFormat => {} bytes; text: {}", size, DocsFormattedText());
    }

    keels2::kh::Service hooks;
    keels2::kh::Target target;
    keels2::kh::Callback callback;
};
}

KEELS2_PLUGIN(docs::HookFormat)
