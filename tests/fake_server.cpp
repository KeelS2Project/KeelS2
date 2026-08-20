#include <keels2/bootstrap_api.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>

#if defined(_WIN32)
#define KEELS2_FAKE_EXPORT __declspec(dllexport)
#else
#define KEELS2_FAKE_EXPORT __attribute__((visibility("default")))
#endif

class CGameEventManager
{
public:
    virtual ~CGameEventManager() = default;

    virtual int LoadEventsFromFile(const char*, bool)
    {
        ++load_count;
        return 1;
    }

    virtual void Reset()
    {
    }

    virtual bool AddListener(void* candidate, const char* name, bool server_side)
    {
        if (!candidate || !name || !name[0] || !server_side)
        {
            return false;
        }
        listener = candidate;
        events.insert(name);
        ++add_count;
        return true;
    }

    virtual bool FindListener(void* candidate, const char* name)
    {
        return candidate && candidate == listener && name && events.contains(name);
    }

    virtual void RemoveListener(void* candidate)
    {
        if (candidate && candidate == listener)
        {
            listener = nullptr;
            events.clear();
            ++remove_count;
        }
    }

    void* listener{};
    std::unordered_set<std::string> events;
    std::uint32_t load_count{};
    std::uint32_t add_count{};
    std::uint32_t remove_count{};
};

namespace
{

bool DispatchGameFrameDuringInit();
bool LoadGameEventsDuringInit();
CGameEventManager g_game_event_manager;

class FakeConfig
{
public:
    virtual bool Connect(KeelCreateInterfaceFn)
    {
        return true;
    }

    virtual void Disconnect()
    {
    }
};

class FakeServer
{
public:
    virtual bool Connect(KeelCreateInterfaceFn)
    {
        return true;
    }

    virtual void Disconnect()
    {
    }

    virtual void* QueryInterface(const char*)
    {
        return nullptr;
    }

    virtual int Init()
    {
        const char* dispatch = std::getenv("KEELS2_TEST_GAME_FRAME_DURING_INIT");
        if (dispatch && std::strcmp(dispatch, "1") == 0 && !DispatchGameFrameDuringInit())
        {
            return 0;
        }
        const char* fail = std::getenv("KEELS2_TEST_SERVER_INIT_FAILURE");
        if (fail && std::strcmp(fail, "1") == 0)
        {
            return 0;
        }
        return LoadGameEventsDuringInit() ? 1 : 0;
    }

    virtual void Shutdown()
    {
    }

    virtual void PreShutdown()
    {
    }

    virtual const void* GetDependencies()
    {
        return nullptr;
    }

    virtual int GetTier()
    {
        return 0;
    }

    virtual void Reconnect(KeelCreateInterfaceFn, const char*)
    {
    }

    virtual bool IsSingleton()
    {
        return true;
    }

    virtual int GetBuildType()
    {
        return 2;
    }

    virtual void Slot11()
    {
    }

    virtual void Slot12()
    {
    }

    virtual void Slot13()
    {
    }

    virtual void Slot14()
    {
    }

    virtual void Slot15()
    {
    }

    virtual void Slot16()
    {
    }

    virtual void Slot17()
    {
    }

    virtual void Slot18()
    {
    }

    virtual void GameFrame(bool, bool, bool)
    {
        ++lifecycle_calls[0];
    }

    std::array<std::uint32_t, 7> lifecycle_calls{};
};

class FakeGameClients
{
public:
    virtual bool Connect(KeelCreateInterfaceFn)
    {
        return true;
    }

    virtual void Disconnect()
    {
    }

    virtual void* QueryInterface(const char*)
    {
        return nullptr;
    }

    virtual int Init()
    {
        return 1;
    }

    virtual void Shutdown()
    {
    }

    virtual void PreShutdown()
    {
    }

    virtual const void* GetDependencies()
    {
        return nullptr;
    }

    virtual int GetTier()
    {
        return 0;
    }

    virtual void Reconnect(KeelCreateInterfaceFn, const char*)
    {
    }

    virtual bool IsSingleton()
    {
        return true;
    }

    virtual int GetBuildType()
    {
        return 2;
    }

    virtual void OnClientConnected(
        std::int32_t,
        const char*,
        std::uint64_t,
        const char*,
        const char*,
        bool)
    {
        ++lifecycle_calls[1];
    }

    virtual bool ClientConnect(
        std::int32_t,
        const char*,
        std::uint64_t,
        const char*,
        bool,
        void*)
    {
        ++client_connect_calls;
        return true;
    }

    virtual void ClientPutInServer(std::int32_t, const char*, std::int32_t, std::uint64_t)
    {
        ++lifecycle_calls[2];
    }

    virtual void ClientActive(std::int32_t, bool, const char*, std::uint64_t)
    {
        ++lifecycle_calls[3];
    }

    virtual void ClientFullyConnect(std::int32_t)
    {
        ++lifecycle_calls[4];
    }

    virtual void ClientDisconnect(
        std::int32_t,
        std::int32_t,
        const char*,
        std::uint64_t,
        const char*)
    {
        ++lifecycle_calls[5];
    }

    virtual void ClientCommand(std::int32_t, const void*)
    {
        ++client_command_calls;
    }

    virtual void ClientSetConVarUserInfoSet()
    {
    }

    virtual void ClientSettingsChanged(std::int32_t)
    {
        ++lifecycle_calls[6];
    }

    std::array<std::uint32_t, 7> lifecycle_calls{};
    std::uint32_t client_connect_calls{};
    std::uint32_t client_command_calls{};
};

FakeConfig g_config;
FakeServer g_server;
FakeGameClients g_game_clients;
unsigned int g_server_queries{};
bool g_init_game_frame_dispatched{};
std::array<char, 300> g_rejection_message{};

template <typename Function>
Function VtableFunction(void* object, std::size_t index)
{
    void* address = (*static_cast<void***>(object))[index];
    Function function{};
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

const auto g_original_game_frame = VtableFunction<void (*)(void*, bool, bool, bool)>(&g_server, 19);

#if defined(_WIN32)
constexpr std::size_t kGameEventLoadSlot = 1;
constexpr std::size_t kGameEventListenerFireSlot = 1;
#else
constexpr std::size_t kGameEventLoadSlot = 2;
constexpr std::size_t kGameEventListenerFireSlot = 2;
#endif

bool DispatchGameFrameDuringInit()
{
    const auto game_frame = VtableFunction<void (*)(void*, bool, bool, bool)>(&g_server, 19);
    if (game_frame == g_original_game_frame)
    {
        return false;
    }
    if (!g_init_game_frame_dispatched)
    {
        g_init_game_frame_dispatched = true;
        game_frame(&g_server, true, false, true);
    }
    return true;
}

bool LoadGameEventsDuringInit()
{
    const char* skip = std::getenv("KEELS2_TEST_SKIP_GAME_EVENT_LOAD");
    if (skip && std::strcmp(skip, "1") == 0)
    {
        return true;
    }
    return VtableFunction<int (*)(void*, const char*, bool)>(
        &g_game_event_manager,
        kGameEventLoadSlot)(&g_game_event_manager, "resource/gameevents.res", true) == 1;
}

}

extern "C" KEELS2_FAKE_EXPORT void* CreateInterface(const char* name, int* return_code)
{
    void* result{};
    if (name && std::strcmp(name, "Source2ServerConfig001") == 0)
    {
        result = &g_config;
    }
    else if (name && std::strcmp(name, "Source2Server001") == 0)
    {
        ++g_server_queries;
        const char* mode = std::getenv("KEELS2_TEST_MISSING_SOURCE2_INTERFACE");
        if (!mode || std::strcmp(mode, "server_after_bootstrap") != 0 || g_server_queries == 1)
        {
            result = &g_server;
        }
    }
    else if (name && std::strcmp(name, "Source2GameClients001") == 0)
    {
        const char* mode = std::getenv("KEELS2_TEST_MISSING_SOURCE2_INTERFACE");
        if (!mode || std::strcmp(mode, "game_clients") != 0)
        {
            result = &g_game_clients;
        }
    }

    if (return_code)
    {
        *return_code = result ? 0 : 1;
    }
    return result;
}

extern "C" KEELS2_FAKE_EXPORT void KeelTest_CompatibilityMarker()
{
}

extern "C" KEELS2_FAKE_EXPORT bool KeelTest_DispatchGameEvent(void* event)
{
    if (!event || !g_game_event_manager.listener ||
        !g_game_event_manager.events.contains("round_start"))
    {
        return false;
    }
    VtableFunction<void (*)(void*, void*)>(
        g_game_event_manager.listener,
        kGameEventListenerFireSlot)(g_game_event_manager.listener, event);
    return true;
}

extern "C" KEELS2_FAKE_EXPORT std::uint32_t KeelTest_GameEventLoadCount()
{
    return g_game_event_manager.load_count;
}

extern "C" KEELS2_FAKE_EXPORT std::uint32_t KeelTest_GameEventAddCount()
{
    return g_game_event_manager.add_count;
}

extern "C" KEELS2_FAKE_EXPORT std::uint32_t KeelTest_GameEventRemoveCount()
{
    return g_game_event_manager.remove_count;
}

extern "C" KEELS2_FAKE_EXPORT bool KeelTest_GameEventListenerActive()
{
    return g_game_event_manager.listener != nullptr;
}

extern "C" KEELS2_FAKE_EXPORT void KeelTest_DispatchLifecycle()
{
    VtableFunction<void (*)(void*, bool, bool, bool)>(&g_server, 19)(
        &g_server,
        true,
        false,
        true);
    VtableFunction<
        void (*)(void*, std::int32_t, const char*, std::uint64_t, const char*, const char*, bool)>(
            &g_game_clients,
            11)(
        &g_game_clients,
        4,
        "Keel",
        76561198000000004ull,
        "STEAM_1:0:2",
        "127.0.0.1:27005",
        false);
    VtableFunction<void (*)(void*, std::int32_t, const char*, std::int32_t, std::uint64_t)>(
        &g_game_clients,
        13)(&g_game_clients, 4, "Keel", 0, 76561198000000004ull);
    VtableFunction<void (*)(void*, std::int32_t, bool, const char*, std::uint64_t)>(
        &g_game_clients,
        14)(&g_game_clients, 4, false, "Keel", 76561198000000004ull);
    VtableFunction<void (*)(void*, std::int32_t)>(&g_game_clients, 15)(&g_game_clients, 4);
    VtableFunction<
        void (*)(void*, std::int32_t, std::int32_t, const char*, std::uint64_t, const char*)>(
            &g_game_clients,
            16)(
        &g_game_clients,
        4,
        39,
        "Keel",
        76561198000000004ull,
        "STEAM_1:0:2");
    VtableFunction<void (*)(void*, std::int32_t)>(&g_game_clients, 19)(&g_game_clients, 4);
}

extern "C" KEELS2_FAKE_EXPORT std::uint32_t KeelTest_LifecycleCallCount(std::uint32_t event)
{
    if (event == 0 || event > 7)
    {
        return 0;
    }
    return event == 1
        ? g_server.lifecycle_calls[0]
        : g_game_clients.lifecycle_calls[event - 1];
}

extern "C" KEELS2_FAKE_EXPORT void KeelTest_ResetLifecycleCalls()
{
    g_server.lifecycle_calls = {};
    g_game_clients.lifecycle_calls = {};
}

extern "C" KEELS2_FAKE_EXPORT bool KeelTest_DispatchClientConnect()
{
    alignas(8) std::array<std::byte, 272> rejection{};
    const std::int32_t allocated = (1 << 30) | 264;
    std::memcpy(rejection.data() + sizeof(std::int32_t), &allocated, sizeof(allocated));
    const bool accepted = VtableFunction<
        bool (*)(void*, std::int32_t, const char*, std::uint64_t, const char*, bool, void*)>(
            &g_game_clients,
            12)(
        &g_game_clients,
        4,
        "Keel",
        76561198000000004ull,
        "STEAM_1:0:2",
        false,
        rejection.data());
    g_rejection_message.fill('\0');
    std::memcpy(
        g_rejection_message.data(),
        rejection.data() + 2 * sizeof(std::int32_t),
        264);
    g_rejection_message[264] = '\0';
    return accepted;
}

extern "C" KEELS2_FAKE_EXPORT void KeelTest_DispatchClientCommand()
{
    const std::array<const char*, 2> arguments{"say", "keels2"};
    alignas(8) std::array<std::byte, 1616> command{};
    const std::int32_t count = static_cast<std::int32_t>(arguments.size());
    const char* const* values = arguments.data();
    std::memcpy(command.data() + 1080, &count, sizeof(count));
    std::memcpy(command.data() + 1088, &values, sizeof(values));
    VtableFunction<void (*)(void*, std::int32_t, const void*)>(&g_game_clients, 17)(
        &g_game_clients,
        4,
        command.data());
}

extern "C" KEELS2_FAKE_EXPORT const char* KeelTest_RejectionMessage()
{
    return g_rejection_message.data();
}

extern "C" KEELS2_FAKE_EXPORT std::uint32_t KeelTest_ClientConnectOriginalCalls()
{
    return g_game_clients.client_connect_calls;
}

extern "C" KEELS2_FAKE_EXPORT std::uint32_t KeelTest_ClientCommandOriginalCalls()
{
    return g_game_clients.client_command_calls;
}
