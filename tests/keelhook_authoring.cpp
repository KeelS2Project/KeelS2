#include "keelhook_virtual_fixture.h"

#include <keels2/keels2.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace
{

KeelHookValue Int32(std::int32_t value)
{
    KeelHookValue output{};
    output.type = KH_VALUE_INT32;
    output.scalar.int32 = value;
    return output;
}

KeelHookValue Pointer(const void* value)
{
    KeelHookValue output{};
    output.type = KH_VALUE_POINTER;
    output.scalar.pointer = const_cast<void*>(value);
    return output;
}

KeelHookValue Boolean(bool value)
{
    KeelHookValue output{};
    output.type = KH_VALUE_BOOL;
    output.scalar.boolean = value ? KEEL_TRUE : KEEL_FALSE;
    return output;
}

KeelHookValue Void()
{
    KeelHookValue output{};
    output.type = KH_VALUE_VOID;
    return output;
}

KeelHookValue Int64(int64 value)
{
    KeelHookValue output{};
    output.type = KH_VALUE_INT64;
    output.scalar.int64 = static_cast<std::int64_t>(value);
    return output;
}

KeelHookValue UInt64(uint64 value)
{
    KeelHookValue output{};
    output.type = KH_VALUE_UINT64;
    output.scalar.uint64 = static_cast<std::uint64_t>(value);
    return output;
}

template <std::size_t Count>
KeelHookFrame Frame(
    KeelHookPhase phase,
    std::array<KeelHookValue, Count>& arguments,
    KeelHookValue result,
    std::uint32_t flags = 0)
{
    return {
        sizeof(KeelHookFrame),
        phase,
        73,
        static_cast<std::uint32_t>(Count),
        flags,
        arguments.data(),
        result
    };
}

struct FreeOwner
{
    keels2::kh::Action OnCall(
        keels2::kh::Call<std::int32_t>& call,
        std::int32_t left,
        std::int32_t& right)
    {
        ++calls;
        phase = call.CurrentPhase();
        original_called = call.OriginalCalled();
        target = call.TargetHandle();
        no_instance = call.Instance<void>() == nullptr;
        observed_left = left;
        if (phase == keels2::kh::Phase::Pre)
        {
            right += 2;
            return keels2::kh::Action::Continue;
        }
        const auto result = call.Result();
        observed_result = result.value_or(-1);
        return call.SetResult(900)
            ? keels2::kh::Action::Override
            : keels2::kh::Action::Continue;
    }

    int calls{};
    keels2::kh::Phase phase{keels2::kh::Phase::Both};
    bool original_called{};
    bool no_instance{};
    KeelHookTargetHandle target{};
    std::int32_t observed_left{};
    std::int32_t observed_result{};
};

struct ObserverOwner
{
    void Observe(bool value)
    {
        ++calls;
        observed = value;
    }

    int calls{};
    bool observed{};
};

struct PluginResultOwner
{
    PluginResult Observe(bool)
    {
        return result;
    }

    PluginResult result{plugin_continue};
};

struct MethodOwner
{
    keels2::kh::Action OnSecond(
        keels2::kh::Call<std::int32_t>& call,
        std::int32_t& value)
    {
        ++calls;
        instance = call.Instance<KeelHookVirtualFixtureInterface>();
        value += 4;
        return keels2::kh::Action::Continue;
    }

    int calls{};
    KeelHookVirtualFixtureInterface* instance{};
};

struct ReferenceOwner
{
    void Observe(const std::int32_t& value)
    {
        ++calls;
        address = &value;
    }

    int calls{};
    const std::int32_t* address{};
};

struct SlotOwner
{
    void Observe(CPlayerSlot& slot)
    {
        ++calls;
        observed = slot.Get();
        slot = CPlayerSlot(slot.Get() + 1);
    }

    int calls{};
    int observed{};
};

struct NativeIntegerOwner
{
    void Observe(int64& signed_value, uint64& unsigned_value)
    {
        ++calls;
        observed_signed = signed_value;
        observed_unsigned = unsigned_value;
        signed_value -= 3;
        unsigned_value += 5;
    }

    int calls{};
    int64 observed_signed{};
    uint64 observed_unsigned{};
};

struct ValveOwner
{
    keels2::kh::Action OnClientConnect(
        keels2::kh::Call<bool>& call,
        CPlayerSlot& slot,
        const char*& name,
        uint64& xuid,
        const char*& network_id,
        bool& unknown,
        CBufferString*& rejection)
    {
        ++connect_calls;
        instance = call.Instance<IServerGameClients>();
        connect_slot = slot.Get();
        connect_name = name;
        connect_xuid = xuid;
        connect_network_id = network_id;
        connect_unknown = unknown;
        connect_rejection = rejection;
        slot = CPlayerSlot(slot.Get() + 1);
        xuid += 2;
        unknown = !unknown;
        return keels2::kh::Action::Continue;
    }

    void OnClientCommand(CPlayerSlot slot, const CCommand& command)
    {
        ++command_calls;
        command_slot = slot.Get();
        observed_command = &command;
    }

    void OnClientDisconnect(
        CPlayerSlot slot,
        ENetworkDisconnectionReason reason,
        const char* name,
        uint64 xuid,
        const char* network_id)
    {
        ++disconnect_calls;
        disconnect_slot = slot.Get();
        disconnect_reason = reason;
        disconnect_name = name;
        disconnect_xuid = xuid;
        disconnect_network_id = network_id;
    }

    int connect_calls{};
    int command_calls{};
    int disconnect_calls{};
    IServerGameClients* instance{};
    int connect_slot{};
    const char* connect_name{};
    uint64 connect_xuid{};
    const char* connect_network_id{};
    bool connect_unknown{};
    CBufferString* connect_rejection{};
    int command_slot{};
    const CCommand* observed_command{};
    int disconnect_slot{};
    ENetworkDisconnectionReason disconnect_reason{};
    const char* disconnect_name{};
    uint64 disconnect_xuid{};
    const char* disconnect_network_id{};
};

struct LeaseBackend
{
    std::mutex mutex;
    bool lease{};
    bool fail_callback{};
    std::uint32_t resolve_calls{};
    std::uint32_t release_calls{};
    std::uint32_t add_calls{};
    std::uint32_t remove_calls{};
    KeelHookCallbackHandle next_callback{1};
    std::unordered_map<KeelHookCallbackHandle, KeelHookCallbackSpec> callbacks;
    std::atomic<bool> block_remove{};
    std::atomic<bool> remove_entered{};
    std::atomic<bool> late_done{};
};

LeaseBackend g_lease_backend;

KeelResult ResolveLease(KeelHookTargetHandle* output)
{
    if (!output)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(g_lease_backend.mutex);
    ++g_lease_backend.resolve_calls;
    g_lease_backend.lease = true;
    *output = 41;
    return KEEL_RESULT_OK;
}

KeelResult ResolveTarget(
    KeelPluginHandle,
    const KeelHookTargetSpec*,
    const KeelHookPrototype*,
    KeelHookTargetHandle* output)
{
    return ResolveLease(output);
}

KeelResult ResolveVirtualTarget(
    KeelPluginHandle,
    const KeelHookVirtualTargetSpec*,
    const KeelHookPrototype*,
    KeelHookTargetHandle* output)
{
    return ResolveLease(output);
}

KeelResult ReleaseTarget(KeelPluginHandle, KeelHookTargetHandle target)
{
    std::scoped_lock lock(g_lease_backend.mutex);
    if (target != 41 || !g_lease_backend.lease)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (!g_lease_backend.callbacks.empty())
    {
        return KEEL_RESULT_BUSY;
    }
    g_lease_backend.lease = false;
    ++g_lease_backend.release_calls;
    return KEEL_RESULT_OK;
}

KeelResult AddCallback(
    KeelPluginHandle,
    KeelHookTargetHandle target,
    const KeelHookCallbackSpec* spec,
    KeelHookCallbackHandle* output)
{
    std::scoped_lock lock(g_lease_backend.mutex);
    ++g_lease_backend.add_calls;
    if (target != 41 || !g_lease_backend.lease || !spec || !spec->callback || !output)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (g_lease_backend.fail_callback)
    {
        g_lease_backend.fail_callback = false;
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    const KeelHookCallbackHandle handle = g_lease_backend.next_callback++;
    g_lease_backend.callbacks.emplace(handle, *spec);
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult RemoveCallback(KeelPluginHandle, KeelHookCallbackHandle callback)
{
    if (g_lease_backend.block_remove.exchange(false, std::memory_order_acq_rel))
    {
        g_lease_backend.remove_entered.store(true, std::memory_order_release);
        g_lease_backend.remove_entered.notify_all();
        while (!g_lease_backend.late_done.load(std::memory_order_acquire))
        {
            g_lease_backend.late_done.wait(false, std::memory_order_acquire);
        }
    }
    std::scoped_lock lock(g_lease_backend.mutex);
    if (g_lease_backend.callbacks.erase(callback) != 1)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    ++g_lease_backend.remove_calls;
    return KEEL_RESULT_OK;
}

const KeelHookApi g_lease_api{
    sizeof(KeelHookApi),
    KEELHOOK_API_VERSION,
    &ResolveTarget,
    &ReleaseTarget,
    &AddCallback,
    &RemoveCallback,
    &ResolveVirtualTarget
};

void MockLog(KeelPluginHandle, KeelLogLevel, const char*)
{
}

KeelResult MockRegisterCommand(
    KeelPluginHandle,
    const KeelCommandSpec*,
    KeelCommandHandle*)
{
    return KEEL_RESULT_UNSUPPORTED;
}

KeelResult MockUnregisterCommand(KeelPluginHandle, KeelCommandHandle)
{
    return KEEL_RESULT_NOT_FOUND;
}

KeelResult MockQuerySource2(
    KeelPluginHandle,
    KeelSource2Capability capability,
    KeelSource2InterfaceInfo* output)
{
    if (!output || output->size != sizeof(KeelSource2InterfaceInfo) ||
        capability != KEELS2_SOURCE2_CAPABILITY_SERVER)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    *output = {
        sizeof(KeelSource2InterfaceInfo),
        KEELS2_SOURCE2_CAPABILITY_SERVER,
        KEELS2_SOURCE2_FACTORY_SERVER,
        KEELS2_SOURCE2_OWNERSHIP_BORROWED,
        KEELS2_SOURCE2_LIFETIME_HOST,
        0,
        KeelHookVirtualFixtureFirst(),
        "Source2Server001",
        "server",
        "/test/server",
        "test"
    };
    return KEEL_RESULT_OK;
}

KeelResult MockQueryNamedSource2(
    KeelPluginHandle,
    KeelSource2Factory,
    const char*,
    KeelSource2InterfaceInfo*)
{
    return KEEL_RESULT_NOT_FOUND;
}

const KeelSource2Api g_source2_api{
    sizeof(KeelSource2Api),
    KEELS2_SOURCE2_API_VERSION,
    &MockQuerySource2,
    &MockQueryNamedSource2
};

KeelResult MockQueryService(
    KeelPluginHandle,
    const char* name,
    std::uint32_t version,
    const void** output)
{
    if (!output)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *output = nullptr;
    if (!name)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (std::strcmp(name, KEELS2_SOURCE2_SERVICE_NAME) == 0)
    {
        if (version != KEELS2_SOURCE2_API_VERSION)
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        *output = &g_source2_api;
        return KEEL_RESULT_OK;
    }
    if (std::strcmp(name, KEELHOOK_SERVICE_NAME) != 0)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (version != KEELHOOK_API_VERSION)
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    *output = &g_lease_api;
    return KEEL_RESULT_OK;
}

const KeelHostApi g_lease_host{
    sizeof(KeelHostApi),
    KEELS2_PLUGIN_ABI_VERSION,
    &MockLog,
    &MockRegisterCommand,
    &MockUnregisterCommand,
    &MockQueryService
};

void ResetLeaseBackend()
{
    std::scoped_lock lock(g_lease_backend.mutex);
    g_lease_backend.lease = false;
    g_lease_backend.fail_callback = false;
    g_lease_backend.resolve_calls = 0;
    g_lease_backend.release_calls = 0;
    g_lease_backend.add_calls = 0;
    g_lease_backend.remove_calls = 0;
    g_lease_backend.next_callback = 1;
    g_lease_backend.callbacks.clear();
    g_lease_backend.block_remove.store(false, std::memory_order_release);
    g_lease_backend.remove_entered.store(false, std::memory_order_release);
    g_lease_backend.late_done.store(false, std::memory_order_release);
}

class LeasePlugin final : public keels2::detail::AbiPlugin
{
public:
    keels2::PluginInfo Information() const noexcept override
    {
        return {"Lease Test", "KeelS2", "1", "Typed lease ownership"};
    }

    bool Load(keels2::Context& context) override
    {
        keels2::kh::Service service;
        if (service.Connect(context) != KEEL_RESULT_OK)
        {
            return false;
        }
        const auto direct = keels2::kh::TargetSpec::Address(
            KeelHookVirtualFixtureFirst());
        {
            keels2::kh::Target target;
            if (service.Resolve<std::int32_t(std::int32_t)>(direct, target) !=
                    KEEL_RESULT_OK || target.Handle() != 41)
            {
                return false;
            }
        }
        if (g_lease_backend.release_calls != 1 || g_lease_backend.lease)
        {
            return false;
        }

        {
            keels2::kh::Target target;
            keels2::kh::Callback callback;
            if (service.Resolve<std::int32_t(std::int32_t)>(direct, target) !=
                    KEEL_RESULT_OK ||
                service.AddCallback<
                    std::int32_t(std::int32_t),
                    &LeasePlugin::OnCall>(
                    target,
                    callback,
                    keels2::kh::Phase::Pre,
                    0,
                    *this) != KEEL_RESULT_OK)
            {
                return false;
            }
        }
        if (g_lease_backend.remove_calls != 1 ||
            g_lease_backend.release_calls != 2 || g_lease_backend.lease)
        {
            return false;
        }

        g_lease_backend.fail_callback = true;
        {
            keels2::kh::Target target;
            keels2::kh::Callback callback;
            if (service.Resolve<std::int32_t(std::int32_t)>(direct, target) !=
                    KEEL_RESULT_OK ||
                service.AddCallback<
                    std::int32_t(std::int32_t),
                    &LeasePlugin::OnCall>(
                    target,
                    callback,
                    keels2::kh::Phase::Pre,
                    0,
                    *this) != KEEL_RESULT_ENGINE_FAILURE)
            {
                return false;
            }
        }
        if (g_lease_backend.release_calls != 3 || g_lease_backend.lease)
        {
            return false;
        }

        auto* instance = static_cast<KeelHookVirtualFixtureInterface*>(
            KeelHookVirtualFixtureFirst());
        g_lease_backend.fail_callback = true;
        keels2::kh::Hook failed;
        if (service.AddVirtualHook<
                &KeelHookVirtualFixtureInterface::First,
                &LeasePlugin::OnCall>(
                instance,
                "test",
                failed,
                keels2::kh::Phase::Pre,
                0,
                *this) != KEEL_RESULT_ENGINE_FAILURE || failed ||
            g_lease_backend.release_calls != 4 || g_lease_backend.lease)
        {
            return false;
        }

        return CheckSharedHooks(service, instance, false) &&
            CheckSharedHooks(service, instance, true) &&
            !g_lease_backend.lease && g_lease_backend.callbacks.empty();
    }

    void Unload(keels2::Context&) noexcept override
    {
    }

private:
    keels2::kh::Action OnCall(
        keels2::kh::Call<std::int32_t>&,
        std::int32_t)
    {
        return keels2::kh::Action::Continue;
    }

    bool CheckSharedHooks(
        keels2::kh::Service& service,
        KeelHookVirtualFixtureInterface* instance,
        bool reverse)
    {
        const std::uint32_t releases = g_lease_backend.release_calls;
        keels2::kh::Hook first;
        keels2::kh::Hook second;
        if (service.AddVirtualHook<
                &KeelHookVirtualFixtureInterface::First,
                &LeasePlugin::OnCall>(
                instance,
                "test",
                first,
                keels2::kh::Phase::Pre,
                0,
                *this) != KEEL_RESULT_OK ||
            service.AddVirtualHook<
                &KeelHookVirtualFixtureInterface::First,
                &LeasePlugin::OnCall>(
                instance,
                "test",
                second,
                keels2::kh::Phase::Post,
                0,
                *this) != KEEL_RESULT_OK ||
            first.TargetHandle() != second.TargetHandle() ||
            first.CallbackHandle() == second.CallbackHandle() ||
            g_lease_backend.callbacks.size() != 2)
        {
            return false;
        }
        keels2::kh::Hook* early = reverse ? &second : &first;
        keels2::kh::Hook* late = reverse ? &first : &second;
        if (early->Reset() != KEEL_RESULT_OK || *early || !*late ||
            g_lease_backend.callbacks.size() != 1 ||
            g_lease_backend.release_calls != releases)
        {
            return false;
        }
        if (service.AddVirtualHook<
                &KeelHookVirtualFixtureInterface::First,
                &LeasePlugin::OnCall>(
                instance,
                "test",
                *early,
                keels2::kh::Phase::Both,
                0,
                *this) != KEEL_RESULT_OK || g_lease_backend.callbacks.size() != 2 ||
            late->Reset() != KEEL_RESULT_OK || early->Reset() != KEEL_RESULT_OK ||
            g_lease_backend.release_calls != releases + 1 || g_lease_backend.lease ||
            !g_lease_backend.callbacks.empty())
        {
            return false;
        }
        return true;
    }
};

std::thread g_late_hook_worker;
std::atomic<bool> g_late_hook_result{};

class FailedHookPlugin final : public keels2::Plugin
{
public:
    static constexpr keels2::PluginInfo Info{
        "Failed Hook Test",
        "KeelS2",
        "1",
        "Failed-load hook admission"
    };

    bool Load() override
    {
        auto* server = GetSource2Server<IServerGameDLL>();
        if (!server || !HookPre(
                server,
                &IServerGameDLL::GameFrame,
                &FailedHookPlugin::OnInitialFrame))
        {
            return false;
        }
        KeelHookCallbackSpec registered{};
        {
            std::scoped_lock lock(g_lease_backend.mutex);
            if (g_lease_backend.callbacks.size() != 1)
            {
                return false;
            }
            registered = g_lease_backend.callbacks.begin()->second;
        }
        std::array arguments{
            Pointer(server),
            Boolean(true),
            Boolean(false),
            Boolean(false)
        };
        KeelHookFrame frame = Frame(KH_PHASE_PRE, arguments, Void());
        if (registered.callback(&frame, registered.user_data) != KH_ACTION_CONTINUE ||
            initial_calls_ != 1)
        {
            return false;
        }
        g_lease_backend.block_remove.store(true, std::memory_order_release);
        g_late_hook_worker = std::thread([this, server] {
            while (!g_lease_backend.remove_entered.load(std::memory_order_acquire))
            {
                g_lease_backend.remove_entered.wait(false, std::memory_order_acquire);
            }
            g_late_hook_result.store(
                HookPre(
                    server,
                    &IServerGameDLL::GameFrame,
                    &FailedHookPlugin::OnLateFrame),
                std::memory_order_release);
            g_lease_backend.late_done.store(true, std::memory_order_release);
            g_lease_backend.late_done.notify_all();
        });
        return false;
    }

private:
    PluginResult OnInitialFrame(
        bool,
        bool,
        bool)
    {
        ++initial_calls_;
        return plugin_continue;
    }

    PluginResult OnLateFrame(
        bool,
        bool,
        bool)
    {
        return plugin_continue;
    }

    int initial_calls_{};
};

using FreeDispatch = keels2::kh::detail::TypedCallback<
    std::int32_t(std::int32_t, std::int32_t),
    false,
    &FreeOwner::OnCall,
    FreeOwner>;
using ObserverDispatch = keels2::kh::detail::TypedCallback<
    void(bool),
    false,
    &ObserverOwner::Observe,
    ObserverOwner>;
using PluginResultDispatch = keels2::kh::detail::TypedCallback<
    void(bool),
    false,
    &PluginResultOwner::Observe,
    PluginResultOwner>;
using MethodDispatch = keels2::kh::detail::TypedCallback<
    std::int32_t(std::int32_t),
    true,
    &MethodOwner::OnSecond,
    MethodOwner>;
using ReferenceDispatch = keels2::kh::detail::TypedCallback<
    void(const std::int32_t&),
    false,
    &ReferenceOwner::Observe,
    ReferenceOwner>;
using SlotDispatch = keels2::kh::detail::TypedCallback<
    void(CPlayerSlot),
    false,
    &SlotOwner::Observe,
    SlotOwner>;
using NativeIntegerDispatch = keels2::kh::detail::TypedCallback<
    void(int64, uint64),
    false,
    &NativeIntegerOwner::Observe,
    NativeIntegerOwner>;
using ClientConnectDispatch = keels2::kh::detail::TypedCallback<
    keels2::kh::MethodSignature<&IServerGameClients::ClientConnect>,
    true,
    &ValveOwner::OnClientConnect,
    ValveOwner>;
using ClientCommandDispatch = keels2::kh::detail::TypedCallback<
    keels2::kh::MethodSignature<&IServerGameClients::ClientCommand>,
    true,
    &ValveOwner::OnClientCommand,
    ValveOwner>;
using ClientDisconnectDispatch = keels2::kh::detail::TypedCallback<
    keels2::kh::MethodSignature<&IServerGameClients::ClientDisconnect>,
    true,
    &ValveOwner::OnClientDisconnect,
    ValveOwner>;

bool CheckFreeDispatch()
{
    FreeOwner owner;
    std::array arguments{Int32(11), Int32(20)};
    KeelHookFrame frame = Frame(KH_PHASE_PRE, arguments, Int32(0));
    if (FreeDispatch::Dispatch(&frame, &owner) != KH_ACTION_CONTINUE ||
        owner.calls != 1 || owner.phase != keels2::kh::Phase::Pre ||
        owner.original_called || !owner.no_instance || owner.target != 73 ||
        owner.observed_left != 11 || arguments[1].scalar.int32 != 22)
    {
        return false;
    }

    frame.phase = KH_PHASE_POST;
    frame.flags = KH_FRAME_ORIGINAL_CALLED;
    frame.result.scalar.int32 = 44;
    if (FreeDispatch::Dispatch(&frame, &owner) != KH_ACTION_OVERRIDE ||
        owner.calls != 2 || owner.phase != keels2::kh::Phase::Post ||
        !owner.original_called || owner.observed_result != 44 ||
        frame.result.scalar.int32 != 900)
    {
        return false;
    }

    ObserverOwner observer;
    std::array observer_arguments{Boolean(true)};
    KeelHookFrame observer_frame = Frame(
        KH_PHASE_PRE,
        observer_arguments,
        Void());
    if (ObserverDispatch::Dispatch(&observer_frame, &observer) != KH_ACTION_CONTINUE ||
        observer.calls != 1 || !observer.observed)
    {
        return false;
    }

    PluginResultOwner result_owner;
    if (PluginResultDispatch::Dispatch(&observer_frame, &result_owner) !=
            KH_ACTION_CONTINUE)
    {
        return false;
    }
    result_owner.result = plugin_override;
    if (PluginResultDispatch::Dispatch(&observer_frame, &result_owner) !=
            KH_ACTION_OVERRIDE)
    {
        return false;
    }
    result_owner.result = plugin_supersede;
    return PluginResultDispatch::Dispatch(&observer_frame, &result_owner) ==
        KH_ACTION_SUPERSEDE;
}

bool CheckMethodAndReferences()
{
    void* raw_instance = KeelHookVirtualFixtureFirst();
    MethodOwner method_owner;
    std::array method_arguments{Pointer(raw_instance), Int32(8)};
    KeelHookFrame method_frame = Frame(
        KH_PHASE_PRE,
        method_arguments,
        Int32(0));
    if (MethodDispatch::Dispatch(&method_frame, &method_owner) != KH_ACTION_CONTINUE ||
        method_owner.calls != 1 || method_owner.instance != raw_instance ||
        method_arguments[1].scalar.int32 != 12)
    {
        return false;
    }

    std::int32_t referenced = 31;
    ReferenceOwner reference_owner;
    std::array reference_arguments{Pointer(&referenced)};
    KeelHookFrame reference_frame = Frame(
        KH_PHASE_PRE,
        reference_arguments,
        Void());
    if (ReferenceDispatch::Dispatch(&reference_frame, &reference_owner) !=
            KH_ACTION_CONTINUE ||
        reference_owner.calls != 1 || reference_owner.address != &referenced)
    {
        return false;
    }

    SlotOwner slot_owner;
    std::array slot_arguments{Int32(4)};
    KeelHookFrame slot_frame = Frame(KH_PHASE_PRE, slot_arguments, Void());
    if (SlotDispatch::Dispatch(&slot_frame, &slot_owner) != KH_ACTION_CONTINUE ||
        slot_owner.calls != 1 || slot_owner.observed != 4 ||
        slot_arguments[0].scalar.int32 != 5)
    {
        return false;
    }

    NativeIntegerOwner native_owner;
    std::array native_arguments{Int64(-9), UInt64(41)};
    KeelHookFrame native_frame = Frame(KH_PHASE_PRE, native_arguments, Void());
    return NativeIntegerDispatch::Dispatch(&native_frame, &native_owner) == KH_ACTION_CONTINUE &&
        native_owner.calls == 1 && native_owner.observed_signed == -9 &&
        native_owner.observed_unsigned == 41 &&
        native_arguments[0].scalar.int64 == -12 &&
        native_arguments[1].scalar.uint64 == 46;
}

bool CheckMalformedFrames()
{
    FreeOwner owner;
    std::array arguments{Int32(1), Int32(2)};
    KeelHookFrame valid = Frame(KH_PHASE_PRE, arguments, Int32(0));
    if (FreeDispatch::Dispatch(nullptr, &owner) != KH_ACTION_CONTINUE ||
        FreeDispatch::Dispatch(&valid, nullptr) != KH_ACTION_CONTINUE)
    {
        return false;
    }

    auto check = [&](KeelHookFrame frame) {
        const int calls = owner.calls;
        return FreeDispatch::Dispatch(&frame, &owner) == KH_ACTION_CONTINUE &&
            owner.calls == calls;
    };

    KeelHookFrame malformed = valid;
    malformed.size = 0;
    if (!check(malformed))
    {
        return false;
    }
    malformed = valid;
    malformed.phase = KH_PHASE_BOTH;
    if (!check(malformed))
    {
        return false;
    }
    malformed = valid;
    malformed.argument_count = 1;
    if (!check(malformed))
    {
        return false;
    }
    malformed = valid;
    malformed.arguments = nullptr;
    if (!check(malformed))
    {
        return false;
    }
    malformed = valid;
    malformed.flags = 2;
    if (!check(malformed))
    {
        return false;
    }
    malformed = valid;
    malformed.result.type = KH_VALUE_UINT32;
    if (!check(malformed))
    {
        return false;
    }
    malformed = valid;
    std::array wrong_type{arguments[0], arguments[1]};
    wrong_type[0].type = KH_VALUE_UINT32;
    malformed.arguments = wrong_type.data();
    if (!check(malformed))
    {
        return false;
    }
    malformed = valid;
    std::array reserved{arguments[0], arguments[1]};
    reserved[0].reserved = 1;
    malformed.arguments = reserved.data();
    if (!check(malformed))
    {
        return false;
    }

    MethodOwner method_owner;
    std::array method_arguments{Pointer(nullptr), Int32(1)};
    KeelHookFrame method_frame = Frame(KH_PHASE_PRE, method_arguments, Int32(0));
    if (MethodDispatch::Dispatch(&method_frame, &method_owner) != KH_ACTION_CONTINUE ||
        method_owner.calls != 0)
    {
        return false;
    }

    ReferenceOwner reference_owner;
    std::array reference_arguments{Pointer(nullptr)};
    KeelHookFrame reference_frame = Frame(
        KH_PHASE_PRE,
        reference_arguments,
        Void());
    return ReferenceDispatch::Dispatch(&reference_frame, &reference_owner) ==
            KH_ACTION_CONTINUE &&
        reference_owner.calls == 0;
}

bool CheckValveDispatch()
{
    ValveOwner owner;
    auto* clients = reinterpret_cast<IServerGameClients*>(
        KeelHookVirtualFixtureFirst());
    const char name[] = "Player";
    const char network_id[] = "STEAM_1:0:7";
    auto* rejection = reinterpret_cast<CBufferString*>(
        static_cast<std::uintptr_t>(0x1000));
    std::array connect_arguments{
        Pointer(clients),
        Int32(3),
        Pointer(name),
        UInt64(55),
        Pointer(network_id),
        Boolean(false),
        Pointer(rejection)
    };
    KeelHookFrame connect_frame = Frame(
        KH_PHASE_PRE,
        connect_arguments,
        Boolean(false));
    if (ClientConnectDispatch::Dispatch(&connect_frame, &owner) != KH_ACTION_CONTINUE ||
        owner.connect_calls != 1 || owner.instance != clients || owner.connect_slot != 3 ||
        owner.connect_name != name || owner.connect_xuid != 55 ||
        owner.connect_network_id != network_id || owner.connect_unknown ||
        owner.connect_rejection != rejection || connect_arguments[1].scalar.int32 != 4 ||
        connect_arguments[3].scalar.uint64 != 57 ||
        connect_arguments[5].scalar.boolean != KEEL_TRUE)
    {
        return false;
    }

    const char* command_arguments[]{"say", "typed"};
    CCommand command(2, command_arguments);
    std::array command_values{
        Pointer(clients),
        Int32(6),
        Pointer(&command)
    };
    KeelHookFrame command_frame = Frame(
        KH_PHASE_PRE,
        command_values,
        Void());
    if (ClientCommandDispatch::Dispatch(&command_frame, &owner) != KH_ACTION_CONTINUE ||
        owner.command_calls != 1 || owner.command_slot != 6 ||
        owner.observed_command != &command)
    {
        return false;
    }

    const auto reason = static_cast<ENetworkDisconnectionReason>(7);
    std::array disconnect_arguments{
        Pointer(clients),
        Int32(8),
        Int32(static_cast<std::int32_t>(reason)),
        Pointer(name),
        UInt64(89),
        Pointer(network_id)
    };
    KeelHookFrame disconnect_frame = Frame(
        KH_PHASE_PRE,
        disconnect_arguments,
        Void());
    return ClientDisconnectDispatch::Dispatch(&disconnect_frame, &owner) ==
            KH_ACTION_CONTINUE &&
        owner.disconnect_calls == 1 && owner.disconnect_slot == 8 &&
        owner.disconnect_reason == reason && owner.disconnect_name == name &&
        owner.disconnect_xuid == 89 && owner.disconnect_network_id == network_id;
}

bool CheckVirtualIndexes()
{
    const auto first = keels2::kh::VirtualIndex<
        &KeelHookVirtualFixtureInterface::First>();
    const auto second = keels2::kh::VirtualIndex<
        &KeelHookVirtualFixtureInterface::Second>();
    const auto aggregate = keels2::kh::VirtualIndex<
        &KeelHookVirtualFixtureInterface::Aggregate>();
    const auto game_frame = keels2::kh::VirtualIndex<&IServerGameDLL::GameFrame>();
    const auto client_connect = keels2::kh::VirtualIndex<
        &IServerGameClients::ClientConnect>();
    const auto client_command = keels2::kh::VirtualIndex<
        &IServerGameClients::ClientCommand>();
    const auto client_disconnect = keels2::kh::VirtualIndex<
        &IServerGameClients::ClientDisconnect>();
    return first && *first == 0 && second && *second == 1 && aggregate && *aggregate == 2 &&
        game_frame && *game_frame == 19 && client_connect && *client_connect == 12 &&
        client_command && *client_command == 17 &&
        client_disconnect && *client_disconnect == 16;
}

}

int main()
{
    if (!CheckFreeDispatch())
    {
        return 1;
    }
    if (!CheckMethodAndReferences())
    {
        return 2;
    }
    if (!CheckMalformedFrames())
    {
        return 3;
    }
    if (!CheckValveDispatch())
    {
        return 4;
    }
    if (!CheckVirtualIndexes())
    {
        return 5;
    }
    if (keels2::detail::AbiPluginAdapter<LeasePlugin>::Load(
            &g_lease_host,
            19) != KEEL_TRUE)
    {
        return 6;
    }
    keels2::detail::AbiPluginAdapter<LeasePlugin>::Unload(19);
    if (g_lease_backend.lease || !g_lease_backend.callbacks.empty() ||
        g_lease_backend.release_calls != 6 || g_lease_backend.remove_calls != 7)
    {
        return 7;
    }
    ResetLeaseBackend();
    g_late_hook_result.store(false, std::memory_order_release);
    if (keels2::detail::AuthoringAdapter<FailedHookPlugin>::Load(
            &g_lease_host,
            23) != KEEL_FALSE)
    {
        return 8;
    }
    if (g_late_hook_worker.joinable())
    {
        g_late_hook_worker.join();
    }
    if (g_late_hook_result.load(std::memory_order_acquire) ||
        !g_lease_backend.remove_entered.load(std::memory_order_acquire) ||
        !g_lease_backend.late_done.load(std::memory_order_acquire) ||
        g_lease_backend.lease || !g_lease_backend.callbacks.empty() ||
        g_lease_backend.resolve_calls != 1 || g_lease_backend.add_calls != 1 ||
        g_lease_backend.remove_calls != 1 || g_lease_backend.release_calls != 1)
    {
        return 9;
    }
    return 0;
}
