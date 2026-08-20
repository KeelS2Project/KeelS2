#include <keels2/keelhook.hpp>

#include <cstdint>

namespace
{

class PeerPlugin final : public keels2::detail::AbiPlugin
{
public:
    keels2::PluginInfo Information() const noexcept override
    {
        return {
            "KeelHook Peer Fixture",
            "KeelS2 Project",
            "1",
            "KeelHook shared-target peer fixture"
        };
    }

    bool Load(keels2::Context& context) override
    {
        if (hooks_.Connect(context) != KEEL_RESULT_OK)
        {
            return false;
        }
#if defined(_WIN32)
        const char* module_name = "01_keelhook_target.dll";
#else
        const char* module_name = "01_keelhook_target.so";
#endif
        const auto spec = keels2::kh::TargetSpec::Symbol(
            module_name,
            "KeelHookFixtureTarget");
        if (hooks_.Resolve<std::int32_t(std::int32_t, std::int32_t)>(spec, target_) !=
                KEEL_RESULT_OK ||
            hooks_.AddCallback<&PeerPlugin::OnHook>(
                target_,
                callback_,
                KH_PHASE_BOTH,
                50,
                *this) != KEEL_RESULT_OK)
        {
            return false;
        }
        context.Log(KEEL_LOG_INFO, "shared physical target joined");
        return true;
    }

    void Unload(keels2::Context& context) noexcept override
    {
        context.Log(
            KEEL_LOG_INFO,
            "peer unload callback ran after automatic cleanup");
    }

private:
    keels2::kh::Action OnHook(keels2::kh::Frame& frame)
    {
        if (!frame || frame.ArgumentCount() != 2)
        {
            return keels2::kh::Action::Continue;
        }
        if (frame.Phase() == KH_PHASE_PRE)
        {
            const auto right = frame.Argument<std::int32_t>(1);
            if (right)
            {
                frame.SetArgument(1, *right + 2);
            }
            return keels2::kh::Action::Continue;
        }
        const auto left = frame.Argument<std::int32_t>(0);
        if (left &&
            (*left == 7001 || *left == 9001 || *left == 10001 || *left == 12001))
        {
            return keels2::kh::Action::Continue;
        }
        frame.SetResult(std::int32_t{900});
        return keels2::kh::Action::Override;
    }

    keels2::kh::Service hooks_;
    keels2::kh::Target target_;
    keels2::kh::Callback callback_;
};

}

KEELS2_DETAIL_EXPOSE_ABI_PLUGIN(PeerPlugin)
