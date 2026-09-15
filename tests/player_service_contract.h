#ifndef KEELS2_PLAYER_SERVICE_CONTRACT_H
#define KEELS2_PLAYER_SERVICE_CONTRACT_H

#include <keels2/players.hpp>
#include <keels2/native_runtime.hpp>
#include <keels2/plugin.hpp>

class PlayerServiceContract
{
public:
    bool Check(keels2::Context& context, const char* stage);
    bool Unloaded(KeelPluginHandle plugin) const;

private:
    const KeelPlayersApi* api_{};
    const KeelPlayerInputApi* input_{};
    std::uint64_t input_context_{};
    keels2::players::Service service_;
    keels2::source2::NativeRuntime runtime_;
    KeelPlayerInfo first_{};
    KeelPlayerInfo current_{};
};

#endif
