#include <keels2/keels2.hpp>

#include <cstdint>
#include <type_traits>

class AuthoringApiContractPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Authoring API Contract",
        "KeelS2",
        "0.5D",
        "Pins the canonical native C++ plugin shape"
    };

    bool Load() override
    {
        return CreateCommand(
            "keels2_authoring_api_contract",
            "Pins member command registration with raw Source-compatible flags",
            &AuthoringApiContractPlugin::Command,
            std::uint64_t{0});
    }

private:
    void Command(const CCommandContext&, const CCommand&)
    {
    }
};

static_assert(std::is_base_of_v<keels2::Plugin, AuthoringApiContractPlugin>);
static_assert(std::is_same_v<
    std::remove_cv_t<decltype(AuthoringApiContractPlugin::Info)>,
    keels2::PluginInfo>);

KEELS2_PLUGIN(AuthoringApiContractPlugin)
