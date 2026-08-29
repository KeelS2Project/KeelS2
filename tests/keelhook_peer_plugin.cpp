#include <keels2/keelhook.hpp>

#include <atomic>
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
            hooks_.AddCallback<
                std::int32_t(std::int32_t, std::int32_t),
                &PeerPlugin::OnHook>(
                target_,
                callback_,
                keels2::kh::Phase::Both,
                50,
                *this) != KEEL_RESULT_OK)
        {
            return false;
        }
        if (hooks_.Resolve<std::int32_t(std::int32_t, std::int32_t)>(spec, observer_target_) !=
                KEEL_RESULT_OK || observer_target_.Handle() != target_.Handle() ||
            hooks_.AddCallback<
                std::int32_t(std::int32_t, std::int32_t),
                &PeerPlugin::OnObserve>(
                observer_target_,
                observer_callback_,
                keels2::kh::Phase::Both,
                25,
                *this) != KEEL_RESULT_OK ||
            callback_.Reset() != KEEL_RESULT_OK || target_.Reset() != KEEL_RESULT_OK ||
            hooks_.Resolve<std::int32_t(std::int32_t, std::int32_t)>(spec, target_) !=
                KEEL_RESULT_OK || target_.Handle() != observer_target_.Handle() ||
            hooks_.AddCallback<
                std::int32_t(std::int32_t, std::int32_t),
                &PeerPlugin::OnHook>(
                target_,
                callback_,
                keels2::kh::Phase::Both,
                50,
                *this) != KEEL_RESULT_OK)
        {
            return false;
        }
        context.Log(KEEL_LOG_INFO, "shared physical target joined");
        context.Log(KEEL_LOG_INFO, "shared typed lease reset and reuse passed");
        return true;
    }

    void Unload(keels2::Context& context) noexcept override
    {
        if (observer_calls_.load(std::memory_order_acquire) != 0)
        {
            context.Log(KEEL_LOG_INFO, "shared typed callbacks dispatched independently");
        }
        context.Log(
            KEEL_LOG_INFO,
            "peer unload callback ran after automatic cleanup");
    }

private:
    keels2::kh::Action OnHook(
        keels2::kh::Call<std::int32_t>& call,
        std::int32_t left,
        std::int32_t& right)
    {
        if (call.CurrentPhase() == keels2::kh::Phase::Pre)
        {
            right += 2;
            return keels2::kh::Action::Continue;
        }
        if (left == 7001 || left == 9001 || left == 10001 || left == 12001)
        {
            return keels2::kh::Action::Continue;
        }
        return call.SetResult(900)
            ? keels2::kh::Action::Override
            : keels2::kh::Action::Continue;
    }

    keels2::kh::Action OnObserve(
        keels2::kh::Call<std::int32_t>&,
        std::int32_t,
        std::int32_t)
    {
        observer_calls_.fetch_add(1, std::memory_order_acq_rel);
        return keels2::kh::Action::Continue;
    }

    keels2::kh::Service hooks_;
    keels2::kh::Target target_;
    keels2::kh::Callback callback_;
    keels2::kh::Target observer_target_;
    keels2::kh::Callback observer_callback_;
    std::atomic<std::uint32_t> observer_calls_{};
};

}

KEELS2_DETAIL_EXPOSE_ABI_PLUGIN(PeerPlugin)
