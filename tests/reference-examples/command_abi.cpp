#include <keels2/authoring.hpp>
#include <keels2/source2_authoring.h>
#include <keels2/source2_runtime.hpp>

#include <cstring>
#include <utility>

namespace docs
{
class CommandAbi final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Docs Command ABI", "KeelS2 documentation", "1.0.0",
        "Portable and Source 2 command callbacks"
    };

    bool Load() override
    {
        const auto& context = HostContext();
        if (runtime.Connect(context) != KEEL_RESULT_OK) return false;

        const KeelCommandSpec spec{
            .size = sizeof(KeelCommandSpec),
            .name = "keel_docs_portable",
            .description = "Print portable arguments",
            .flags = 0,
            .callback = [](const KeelCommandInvocation* invocation, void* user_data) {
                static_cast<CommandAbi*>(user_data)->Portable(keels2::CommandInvocation(invocation));
            },
            .user_data = this
        };
        if (context.RegisterCommand(spec, portable) != KEEL_RESULT_OK) return false;

        keels2::Command pending;
        if (context.RegisterCommand<&CommandAbi::Portable>(pending,
                "keel_docs_member", "Print portable arguments through a member callback", *this)
            != KEEL_RESULT_OK) return false;
        member = std::move(pending);

        const void* service{};
        if (context.QueryService(KEELS2_SOURCE2_AUTHORING_SERVICE_NAME,
                KEELS2_SOURCE2_AUTHORING_API_VERSION, &service) != KEEL_RESULT_OK) return false;
        native_api = static_cast<const KeelSource2AuthoringApi*>(service);
        if (!native_api || native_api->size != sizeof(KeelSource2AuthoringApi) ||
            native_api->api_version != KEELS2_SOURCE2_AUTHORING_API_VERSION ||
            !native_api->register_command || !native_api->unregister_command) return false;

        KeelSource2CommandSpec native{};
        native.size = sizeof(native);
        native.reserved = 0;
        native.name = "keel_docs_native";
        native.description = "Show native arguments; server console may echo or retire commands";
        native.flags = 0;
        native.callback = [](const void* caller, const void* command, void* user_data) {
            if (!caller || !command || !user_data) return;
            static_cast<CommandAbi*>(user_data)->Native(
                *static_cast<const CCommandContext*>(caller),
                *static_cast<const CCommand*>(command));
        };
        native.user_data = this;
        return native_api->register_command(context.PluginHandle(), &native, &native_handle)
            == KEEL_RESULT_OK;
    }

    void Unload() override
    {
        static_cast<void>(portable.Reset());
        static_cast<void>(member.Reset());
        native_handle = 0;
        native_api = nullptr;
    }

private:
    void Portable(const keels2::CommandInvocation& invocation)
    {
        if (!invocation || !invocation.Name()) return;
        LogMessage("{}: {} arguments", invocation.Name(), invocation.Size());
        for (std::size_t index = 0; index < invocation.Size(); ++index)
            if (const char* argument = invocation[index])
                LogMessage("Argument {}: {}", index, argument);
    }

    void Native(const CCommandContext& caller, const CCommand& command)
    {
        LogMessage("Native caller slot {}; ArgC {}", caller.GetPlayerSlot().Get(), command.ArgC());
        if (caller.GetPlayerSlot().Get() != -1 || command.ArgC() != 2) return;
        if (std::strcmp(command[1], "echo") == 0)
        {
            LogMessage("Server command result: {}",
                runtime.ServerCommand("echo KeelS2 command example\n"));
        }
        else if (std::strcmp(command[1], "retire") == 0)
        {
            const KeelResult first = portable.Reset();
            const KeelResult second = member.Reset();
            const KeelResult third = native_api->unregister_command(
                HostContext().PluginHandle(), native_handle);
            if (third == KEEL_RESULT_OK || third == KEEL_RESULT_NOT_FOUND) native_handle = 0;
            LogMessage("Retirement results: {}, {}, {}", first, second, third);
        }
    }

    keels2::Command portable;
    keels2::Command member;
    keels2::source2::Runtime runtime;
    const KeelSource2AuthoringApi* native_api{};
    KeelCommandHandle native_handle{};
};
}

KEELS2_PLUGIN(docs::CommandAbi)
