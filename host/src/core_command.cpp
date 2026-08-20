#include "host.h"

#include <algorithm>

namespace keels2::host
{

void Host::DispatchCoreCommand(
    const KeelCommandInvocation& invocation,
    std::unique_lock<std::recursive_mutex>& state_lock)
{
    if (invocation.argument_count == 0)
    {
        ShowMainMenu();
        return;
    }
    if (!invocation.arguments || !invocation.arguments[0])
    {
        Write(KEEL_LOG_ERROR, "invalid keel command arguments");
        return;
    }

    const std::string_view command(invocation.arguments[0]);
    if (EqualInsensitive(command, "help"))
    {
        if (invocation.argument_count == 1)
        {
            ShowMainMenu();
        }
        else if (invocation.arguments[1] && EqualInsensitive(invocation.arguments[1], "plugins"))
        {
            ShowPluginsMenu();
        }
        else
        {
            Write(KEEL_LOG_ERROR, "unknown help topic");
        }
        return;
    }
    if (EqualInsensitive(command, "plugins"))
    {
        if (invocation.argument_count == 1)
        {
            ShowPluginsMenu();
            return;
        }
        if (!invocation.arguments[1])
        {
            Write(KEEL_LOG_ERROR, "invalid plugins subcommand");
            return;
        }
        const std::string_view subcommand(invocation.arguments[1]);
        if (EqualInsensitive(subcommand, "list"))
        {
            if (invocation.argument_count == 2)
            {
                ShowPluginList();
            }
            else
            {
                WriteUsage("keel plugins list");
            }
        }
        else if (EqualInsensitive(subcommand, "info"))
        {
            if (invocation.argument_count == 3 && invocation.arguments[2])
            {
                ShowPluginInfo(invocation.arguments[2]);
            }
            else
            {
                WriteUsage("keel plugins info <plugin>");
            }
        }
        else if (EqualInsensitive(subcommand, "cmds"))
        {
            if (invocation.argument_count == 2)
            {
                ShowPluginCommands(nullptr);
            }
            else if (invocation.argument_count == 3 && invocation.arguments[2])
            {
                PluginRecord* plugin = SelectPlugin(invocation.arguments[2]);
                if (plugin)
                {
                    ShowPluginCommands(plugin);
                }
            }
            else
            {
                WriteUsage("keel plugins cmds [plugin]");
            }
        }
        else if (EqualInsensitive(subcommand, "load"))
        {
            if (invocation.argument_count == 3 && invocation.arguments[2])
            {
                LoadPluginCommand(invocation.arguments[2], state_lock);
            }
            else
            {
                WriteUsage("keel plugins load <file>");
            }
        }
        else if (EqualInsensitive(subcommand, "unload"))
        {
            if (invocation.argument_count == 3 && invocation.arguments[2])
            {
                UnloadPluginCommand(invocation.arguments[2], state_lock);
            }
            else
            {
                WriteUsage("keel plugins unload <plugin>");
            }
        }
        else if (EqualInsensitive(subcommand, "pause"))
        {
            if (invocation.argument_count == 3 && invocation.arguments[2])
            {
                PausePluginCommand(invocation.arguments[2], state_lock);
            }
            else
            {
                WriteUsage("keel plugins pause <plugin>");
            }
        }
        else if (EqualInsensitive(subcommand, "resume"))
        {
            if (invocation.argument_count == 3 && invocation.arguments[2])
            {
                ResumePluginCommand(invocation.arguments[2], state_lock);
            }
            else
            {
                WriteUsage("keel plugins resume <plugin>");
            }
        }
        else
        {
            Write(KEEL_LOG_ERROR, "unknown or incomplete plugins subcommand; use keel plugins");
        }
        return;
    }
    if (EqualInsensitive(command, "version") && invocation.argument_count == 1)
    {
        ShowVersion();
    }
    else if (EqualInsensitive(command, "game") && invocation.argument_count == 1)
    {
        ShowGame();
    }
    else if (EqualInsensitive(command, "status") && invocation.argument_count == 1)
    {
        ShowStatus();
    }
    else if (EqualInsensitive(command, "credits") && invocation.argument_count == 1)
    {
        ShowCredits();
    }
    else
    {
        Write(KEEL_LOG_ERROR, "unknown or incomplete keel command; use keel help");
    }
}

void Host::ShowMainMenu()
{
    WriteLine("KeelS2 Menu");
    WriteLine("Usage: keel <command> [arguments]");
    WriteLine("  plugins - Manage and inspect plugins");
    WriteLine("  game    - List information about the game");
    WriteLine("  status  - Show host and plugin status");
    WriteLine("  credits - About KeelS2");
    WriteLine("  version - Show version information");
    WriteLine("  help    - Show this menu or a command menu");
}

void Host::ShowPluginsMenu()
{
    WriteLine("KeelS2 Plugins Menu");
    WriteLine("Usage: keel plugins <command> [arguments]");
    WriteLine("  info <plugin>   - Show information about a plugin");
    WriteLine("  list            - List loaded, invalid, and failed plugins");
    WriteLine("  cmds [plugin]   - List registered plugin commands");
    WriteLine("  load <file>     - Load a plugin module");
    WriteLine("  pause <plugin>  - Pause a running plugin");
    WriteLine("  resume <plugin> - Resume a paused plugin");
    WriteLine("  unload <plugin> - Unload a loaded plugin");
}

void Host::ShowVersion()
{
    WriteLine("KeelS2 " + std::string(kHostVersion));
    WriteLine("Plugin ABI: " + std::to_string(KEELS2_PLUGIN_ABI_VERSION));
}

void Host::ShowGame()
{
    WriteLine("Game: " + game_);
    WriteLine("Runtime version: " + game_version_);
    WriteLine("Platform: " + platform_);
    WriteLine("Compatibility profile: " + compatibility_profile_);
}

void Host::ShowStatus()
{
    std::size_t loaded{};
    std::size_t paused{};
    std::size_t invalid{};
    std::size_t failed{};
    for (const auto& plugin : plugins_)
    {
        if (plugin->state == PluginState::loaded)
        {
            ++loaded;
        }
        else if (plugin->state == PluginState::paused)
        {
            ++paused;
        }
        else if (plugin->state == PluginState::invalid)
        {
            ++invalid;
        }
        else
        {
            ++failed;
        }
    }
    const std::size_t plugin_commands = static_cast<std::size_t>(std::count_if(
        commands_.begin(), commands_.end(), [](const auto& entry) {
            return entry.second->owner != 0;
        }
    ));
    WriteLine("KeelS2 status: running");
    WriteLine("Game: " + game_ + " " + game_version_ + " (" + platform_ + ")");
    WriteLine("Profile: " + compatibility_profile_);
    WriteLine(
        "Plugins: " + std::to_string(plugins_.size()) + " total, " +
        std::to_string(loaded) + " loaded, " + std::to_string(invalid) +
        " invalid, " + std::to_string(failed) + " error, " +
        std::to_string(paused) + " paused"
    );
    WriteLine("Plugin commands: " + std::to_string(plugin_commands));
}

void Host::ShowCredits()
{
    WriteLine("KeelS2 is developed by the KeelS2 Project.");
    WriteLine("https://keels2.com");
}

void Host::ShowPluginList()
{
    WriteLine("Listing " + std::to_string(plugins_.size()) + " plugins:");
    for (const auto& plugin : plugins_)
    {
        std::string label = plugin->name.empty() ? plugin->path.filename().string() : plugin->name;
        std::string line = "  [" + PluginDisplayId(plugin.get()) + "] " + label;
        if (!plugin->version.empty())
        {
            line += " (" + plugin->version + ")";
        }
        if (!plugin->author.empty())
        {
            line += " by " + plugin->author;
        }
        line += " - ";
        line += PluginStateLabel(plugin->state);
        WriteLine(line);
    }
}

void Host::ShowPluginInfo(std::string_view selector)
{
    PluginRecord* plugin = SelectPlugin(selector);
    if (!plugin)
    {
        return;
    }
    WriteLine("Plugin [" + PluginDisplayId(plugin) + "]");
    WriteLine("  State: " + std::string(PluginStateLabel(plugin->state)));
    if (!plugin->name.empty())
    {
        WriteLine("  Name: " + plugin->name);
        WriteLine("  Version: " + plugin->version);
        WriteLine("  Author: " + (plugin->author.empty() ? std::string("unknown") : plugin->author));
        WriteLine("  Description: " + (plugin->description.empty() ? std::string("none") : plugin->description));
        WriteLine("  Commands: " + std::to_string(PluginCommandCount(plugin->handle)));
    }
    if (plugin->state == PluginState::invalid || plugin->state == PluginState::error)
    {
        WriteLine("  File: " + plugin->path.filename().string());
        WriteLine("  Diagnostic: " + plugin->diagnostic);
    }
}

void Host::ShowPluginCommands(const PluginRecord* plugin)
{
    std::vector<const CommandRecord*> matches;
    for (const auto& entry : commands_)
    {
        if (entry.second->owner != 0 && (!plugin || entry.second->owner == plugin->handle))
        {
            matches.push_back(entry.second.get());
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto* left, const auto* right) {
        return left->name < right->name;
    });
    if (plugin)
    {
        WriteLine("Commands for [" + PluginDisplayId(plugin) + "] " + plugin->name + ":");
    }
    else
    {
        WriteLine("Listing " + std::to_string(matches.size()) + " plugin commands:");
    }
    for (const CommandRecord* command : matches)
    {
        const PluginRecord* owner = PluginByHandle(command->owner);
        std::string line = "  " + command->name;
        if (!command->description.empty())
        {
            line += " - " + command->description;
        }
        if (!plugin && owner)
        {
            line += " [" + PluginDisplayId(owner) + "]";
        }
        WriteLine(line);
    }
}

}
