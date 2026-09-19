#include <keels2/authoring.hpp>
#include <optional>

namespace docs
{
using namespace keels2::authoring;

class InputWatch final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Docs Input Watch", "KeelS2 documentation", "1.0.0",
        "Observe a client's Use action without consuming input"};

    bool Load() override
    {
        ResetWatch();
        return CreateCommand("keel_docs_input", "Watch the caller's Use action", &InputWatch::Watch,
            FCVAR_GAMEDLL | FCVAR_CLIENT_CAN_EXECUTE);
    }

    void OnGameFrame(bool, bool, bool) override
    {
        if (!watched)
            return;

        PlayerInput input;

        if (!GetPlayerInput(*watched, input))
        {
            LogWarning("Input watch stopped: {}", LastError());
            ResetWatch();
            return;
        }

        if (previous && previous->context == input.context &&
            input.Down(PlayerButton::Use) && !previous->Down(PlayerButton::Use))
            LogMessage("Use pressed in slot {}.", watched->slot.Get());

        previous = input;
    }

    void OnLevelShutdown() override
    {
        ResetWatch();
    }

    bool PreparePause() override
    {
        ResetWatch();
        return true;
    }

private:
    void Watch(const CCommandContext& context, const CCommand& command)
    {
        if (command.ArgC() != 1)
        {
            LogMessage("Usage: keel_docs_input");
            return;
        }

        PlayerInfo player;

        if (!GetPlayer(context.GetPlayerSlot(), player))
        {
            LogMessage("Run keel_docs_input from a connected client's console.");
            return;
        }

        watched = player.Connection();
        previous.reset();
        LogMessage("Watching Use in slot {}. Client binds and gameplay input stay active.", player.slot.Get());
    }

    void ResetWatch()
    {
        watched.reset();
        previous.reset();
    }

    std::optional<PlayerConnection> watched;
    std::optional<PlayerInput> previous;
};
}

KEELS2_PLUGIN(docs::InputWatch)
