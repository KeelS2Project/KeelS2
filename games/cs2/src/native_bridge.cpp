#include <keels2/cs2/native_bridge.h>
#include <keels2/cs2/cvar_abi.h>

#include <igameevents.h>
#include <tier1/bufferstring.h>
#include <tier1/convar.h>

#include <algorithm>
#include <cstdint>
#include <new>
#include <type_traits>

static_assert(sizeof(ConVarRef) == sizeof(keels2::cs2::ConVarRef));
static_assert(alignof(ConVarRef) == alignof(keels2::cs2::ConVarRef));
static_assert(std::is_trivially_copyable_v<ConVarRef>);
static_assert(sizeof(CConVarRef<bool>) == sizeof(keels2::cs2::ConVarObject));
static_assert(sizeof(CConVarRef<std::int32_t>) == sizeof(keels2::cs2::ConVarObject));
static_assert(sizeof(CConVarRef<float>) == sizeof(keels2::cs2::ConVarObject));
static_assert(sizeof(CConVarRef<CUtlString>) == sizeof(keels2::cs2::ConVarObject));

namespace
{

class GameEventListener final : public IGameEventListener2
{
public:
    GameEventListener(
        IGameEventManager2& manager,
        KeelCs2GameEventCallback callback,
        void* user_data)
        : manager_(manager), callback_(callback), user_data_(user_data)
    {
    }

    ~GameEventListener() override
    {
        manager_.RemoveListener(this);
    }

    void FireGameEvent(IGameEvent* event) override
    {
        if (event && callback_)
        {
            callback_(event, event->GetName(), user_data_);
        }
    }

    bool Listen(const char* name)
    {
        return name && name[0] && manager_.AddListener(this, name, true);
    }

private:
    IGameEventManager2& manager_;
    KeelCs2GameEventCallback callback_{};
    void* user_data_{};
};

}

extern "C" void* KeelCs2_CreateGameEventListener(
    void* manager,
    KeelCs2GameEventCallback callback,
    void* user_data)
{
    if (!manager || !callback)
    {
        return nullptr;
    }
    try
    {
        return new GameEventListener(
            *static_cast<IGameEventManager2*>(manager),
            callback,
            user_data);
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" void KeelCs2_DestroyGameEventListener(void* listener)
{
    delete static_cast<GameEventListener*>(listener);
}

extern "C" std::uint32_t KeelCs2_ListenForGameEvent(void* listener, const char* name)
{
    try
    {
        return listener && static_cast<GameEventListener*>(listener)->Listen(name) ? 1u : 0u;
    }
    catch (...)
    {
        return 0;
    }
}

extern "C" std::uint32_t KeelCs2_WriteRejectionMessage(
    void* buffer,
    const char* message,
    std::uint32_t length)
{
    if (!buffer || !message)
    {
        return 0;
    }
    try
    {
        auto* destination = static_cast<CBufferString*>(buffer);
        destination->Clear();
        const auto bounded = static_cast<int>(std::min<std::uint32_t>(length, 255u));
        destination->Insert(0, message, bounded);
        return 1;
    }
    catch (...)
    {
        return 0;
    }
}
