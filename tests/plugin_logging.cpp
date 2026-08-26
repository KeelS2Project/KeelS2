#include <keels2/plugin.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr KeelPluginHandle kPlugin{77};
constexpr const char* kFormattingFailure = "[KeelS2] log formatting failed";
constexpr const char* kThrowingSinkMessage = "throwing host sink";

struct LogEntry
{
    KeelPluginHandle plugin{};
    KeelLogLevel level{};
    std::string message;
};

std::vector<LogEntry> g_logs;

void Log(KeelPluginHandle plugin, KeelLogLevel level, const char* message)
{
    if (message && std::string_view{message} == kThrowingSinkMessage)
    {
        throw 23;
    }
    if (message)
    {
        g_logs.push_back({plugin, level, message});
    }
}

KeelResult RegisterCommand(
    KeelPluginHandle,
    const KeelCommandSpec*,
    KeelCommandHandle*)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult UnregisterCommand(KeelPluginHandle, KeelCommandHandle)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult QueryService(
    KeelPluginHandle,
    const char*,
    std::uint32_t,
    const void** service)
{
    if (service)
    {
        *service = nullptr;
    }
    return KEEL_RESULT_NOT_FOUND;
}

enum class ProbeEnum : unsigned
{
    value = 7
};

struct ProbeSlot
{
    int Get() const noexcept
    {
        return 3;
    }
};

struct ThrowingValue
{
    int Get() const
    {
        throw 17;
    }
};

extern const char g_external_text[];

class LoggingPlugin final : public keels2::detail::AbiPlugin
{
public:
    keels2::PluginInfo Information() const noexcept override
    {
        return {
            "Logging Contract",
            "KeelS2 Tests",
            "0.7.0",
            "Validates formatted Context logging"
        };
    }

    bool Load(keels2::Context& context) override
    {
        const std::string owned{"owned\0ignored", 13};
        const std::string_view view{"view\0ignored", 12};
        const char* null_text{};
        const char bounded[]{'o', 'k'};

        context.Log(
            KEEL_LOG_INFO,
            "literal={} owned={} view={} null={} bounded={} external={} bool={} "
            "signed={} unsigned={} float={} enum={} slot={} escaped={{value}}",
            "text",
            owned,
            view,
            null_text,
            bounded,
            g_external_text,
            true,
            -42,
            std::uint64_t{99},
            1.25F,
            ProbeEnum::value,
            ProbeSlot{});

        context.Log(KEEL_LOG_INFO, kThrowingSinkMessage);
        context.Log(KEEL_LOG_INFO, "{}");
        context.Log(KEEL_LOG_WARNING, "missing={} {}", 1);
        context.Log(KEEL_LOG_ERROR, "excess={}", 1, 2);
        context.Log(KEEL_LOG_INFO, "malformed={", 1);
        context.Log(KEEL_LOG_WARNING, "malformed=}", 1);
        context.Log(KEEL_LOG_ERROR, "throwing={}", ThrowingValue{});
        context.Log(KEEL_LOG_INFO, "after={}", "contained");
        return true;
    }

    void Unload(keels2::Context&) noexcept override
    {
    }
};

const char g_external_text[] = "external";

using Adapter = keels2::detail::AbiPluginAdapter<LoggingPlugin>;

bool Matches(std::size_t index, KeelLogLevel level, const char* message)
{
    return index < g_logs.size() && g_logs[index].plugin == kPlugin &&
        g_logs[index].level == level && g_logs[index].message == message;
}

}

static_assert(noexcept(std::declval<const keels2::Context&>().Log(
    KEEL_LOG_INFO,
    "value={}",
    1)));

int main()
{
    const KeelHostApi api{
        sizeof(KeelHostApi),
        KEELS2_PLUGIN_ABI_VERSION,
        &Log,
        &RegisterCommand,
        &UnregisterCommand,
        &QueryService
    };

    g_logs.clear();
    if (Adapter::Load(&api, kPlugin) != KEEL_TRUE)
    {
        return 1;
    }
    Adapter::Unload(kPlugin);

    if (g_logs.size() != 8)
    {
        return 2;
    }
    if (!Matches(
            0,
            KEEL_LOG_INFO,
            "literal=text owned=owned view=view null=(null) bounded=ok external=external "
            "bool=true signed=-42 unsigned=99 float=1.25 enum=7 slot=3 escaped={value}"))
    {
        return 3;
    }
    if (!Matches(1, KEEL_LOG_INFO, "{}"))
    {
        return 4;
    }
    if (!Matches(2, KEEL_LOG_WARNING, kFormattingFailure))
    {
        return 5;
    }
    if (!Matches(3, KEEL_LOG_ERROR, kFormattingFailure))
    {
        return 6;
    }
    if (!Matches(4, KEEL_LOG_INFO, kFormattingFailure))
    {
        return 7;
    }
    if (!Matches(5, KEEL_LOG_WARNING, kFormattingFailure))
    {
        return 8;
    }
    if (!Matches(6, KEEL_LOG_ERROR, kFormattingFailure))
    {
        return 9;
    }
    if (!Matches(7, KEEL_LOG_INFO, "after=contained"))
    {
        return 10;
    }

    return 0;
}
