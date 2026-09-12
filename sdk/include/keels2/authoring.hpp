#ifndef KEELS2_AUTHORING_HPP
#define KEELS2_AUTHORING_HPP

#include <keels2/keels2.hpp>

namespace keels2::authoring
{

using keels2::Plugin;
using keels2::PluginInfo;
using keels2::ConVar;
using Action = kh::Action;
using keels2::Entity;
using keels2::SchemaField;
using keels2::PlayerInfo;
using keels2::PlayerConnection;

template <typename Return>
using HookCall = kh::Call<Return>;

inline constexpr Action PLUGIN_CONTINUE = Action::Continue;
inline constexpr Action PLUGIN_OVERRIDE = Action::Override;
inline constexpr Action PLUGIN_SUPERSEDE = Action::Supersede;

}

#endif
