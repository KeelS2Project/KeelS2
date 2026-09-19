#ifndef KEELS2_CS2_NATIVE_INPUT_H
#define KEELS2_CS2_NATIVE_INPUT_H
#include <keels2/game_adapter.hpp>
#include <keels2/cs2/native_bridge.h>
namespace keels2::cs2 {
class InputEnvironment
{
public:
    virtual ~InputEnvironment() = default;
    virtual KeelResult InputCapabilities(std::uint32_t& direct, std::uint32_t& queued) = 0;
    // No engine callbacks; rejects shutdown and stale epochs.
    virtual KeelResult InputCurrent(std::uint64_t expected, void*&, std::uint64_t&) noexcept = 0;
    virtual KeelCs2EntityInputBindings InputBindings() const noexcept = 0;
};
class NativeInputBackend final
{
public:
    explicit NativeInputBackend(InputEnvironment& environment) : environment_(environment) {}
    KeelResult Dispatch(const host::GameEntityInputRequest&, KeelBool& invoked);
private:
    InputEnvironment& environment_;
};
}
#endif
