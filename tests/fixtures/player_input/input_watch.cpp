#include <keels2/authoring.hpp>

using namespace keels2::authoring;

class InputWatch final : public Plugin
{
public:
    static constexpr PluginInfo Info{"Input Watch", "KeelS2 documentation", "1.0.0", "Read Forward and Use actions"};

    bool Load() override
    {
        return CreateCommand("input_watch", "Toggle input observation for this connection", &InputWatch::Watch);
    }

    void OnGameFrame(bool simulating, bool, bool) override
    {
        if (!simulating || !watched_.generation)
            return;

        PlayerInfo player;

        if (!GetPlayer(watched_, player))
        {
            watched_ = {};
            previous_ = {};
            return;
        }

        PlayerInput current;

        if (!GetPlayerInput(watched_, current))
        {
            previous_ = {};
            return;
        }

        const bool sameContext = previous_.context == current.context;
        const bool changed = current.Down(PlayerButton::Forward) != previous_.Down(PlayerButton::Forward) ||
            current.Down(PlayerButton::Use) != previous_.Down(PlayerButton::Use);

        previous_ = current;

        if (sameContext && changed)
        {
            SendToConsole(watched_.slot, "Forward: {} | Use: {}\n",
                current.Down(PlayerButton::Forward) ? "held" : "released",
                current.Down(PlayerButton::Use) ? "held" : "released");
        }
    }

    void OnPluginResumed(const PluginSnapshot& plugin) override
    {
        if (plugin.id == HostContext().PluginHandle())
            previous_ = {};
    }

private:
    PlayerConnection watched_{};
    PlayerInput previous_{};

    void Watch(const CCommandContext& context, const CCommand&)
    {
        PlayerInfo player;

        if (!GetPlayer(context.GetPlayerSlot(), player))
        {
            LogMessage("Run input_watch in a connected player's console.");
            return;
        }

        previous_ = {};

        if (watched_.slot.Get() == player.slot.Get() && watched_.generation == player.connection)
        {
            watched_ = {};
            SendToConsole(player.slot, "Input observation stopped.\n");
            return;
        }

        watched_ = player.Connection();
        GetPlayerInput(watched_, previous_);
        SendToConsole(player.slot, "Watching Forward and Use. Run input_watch again to stop.\n");
    }
};

KEELS2_PLUGIN(InputWatch)
