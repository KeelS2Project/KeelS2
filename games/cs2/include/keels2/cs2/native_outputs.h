#ifndef KEELS2_CS2_NATIVE_OUTPUTS_H
#define KEELS2_CS2_NATIVE_OUTPUTS_H
#include <keels2/game_adapter.hpp>
#include <keels2/cs2/native_bridge.h>
#include <array>
namespace keels2::cs2 {
class OutputEnvironment
{
public:
    virtual ~OutputEnvironment() = default;
    virtual bool OutputOnThread() const noexcept = 0;
    virtual KeelResult OutputCurrent(std::uint64_t expected, void*& system, std::uint64_t& epoch) noexcept = 0;
};

class NativeOutputHooks final
{
public:
    explicit NativeOutputHooks(OutputEnvironment& environment) : environment_(environment) {}

    KeelResult Start(void* function, const char* profile, const KeelHookApi&, host::GameHookDefer,
        host::GameEntityOutputCallback, void*);

    KeelResult Stop();

    bool Started() const noexcept
    {
        return hook_ != 0;
    }

private:
    struct Pending
    {
        NativeOutputHooks* owner{};
        KeelHookFrame* frame{};
        std::uint64_t token{}, epoch{};
        KeelCs2EntityOutputContext native{};
        KeelEntityOutputEvent event{};
    };
    static KeelHookAction Entry(KeelHookFrame*, void*);
    static void Complete(void*) noexcept;
    KeelHookAction Dispatch(KeelHookFrame&);
    OutputEnvironment& environment_;
    KeelHookApi hooks_{};
    host::GameHookDefer defer_{};
    host::GameEntityOutputCallback callback_{};
    void* user_data_{};
    void* function_{};
    KeelHookTargetHandle target_{};
    KeelHookCallbackHandle hook_{};
    std::uint64_t next_token_{1};
    std::array<Pending,64> pending_{};
};
}
#endif
