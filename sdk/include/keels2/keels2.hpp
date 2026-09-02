#ifndef KEELS2_KEELS2_HPP
#define KEELS2_KEELS2_HPP

#include <keels2/convar.h>
#include <keels2/entities.hpp>
#include <keels2/lifecycle.h>
#include <keels2/plugin.hpp>
#include <keels2/plugins.h>
#include <keels2/schema.hpp>
#include <keels2/services.hpp>
#include <keels2/source2.hpp>
#include <keels2/source2_runtime.hpp>
#include <keels2/source2_authoring.h>
#include <keels2/source2_callbacks.h>
#include <keels2/source2_hooks.hpp>
#include <keels2/source2_lifecycle.hpp>
#include <keels2/source2_sdk.hpp>

#include <KeyValues.h>
#include <igameevents.h>
#include <iloopmode.h>
#include <tier1/bufferstring.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using PluginId = KeelPluginHandle;

enum class PluginStatus : std::uint32_t
{
    unknown = KEELS2_PLUGIN_STATE_UNKNOWN,
    loading = KEELS2_PLUGIN_STATE_LOADING,
    running = KEELS2_PLUGIN_STATE_RUNNING,
    paused = KEELS2_PLUGIN_STATE_PAUSED,
    invalid = KEELS2_PLUGIN_STATE_INVALID,
    error = KEELS2_PLUGIN_STATE_ERROR
};

struct PluginSnapshot
{
    PluginId id{};
    PluginStatus status{PluginStatus::unknown};
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    std::string file;
    std::string diagnostic;
};

enum class DependencyRequirement : std::uint32_t
{
    exact = KEELS2_PLUGIN_DEPENDENCY_EXACT,
    at_least = KEELS2_PLUGIN_DEPENDENCY_AT_LEAST
};

struct PluginDependency
{
    std::string name;
    std::string version;
    DependencyRequirement requirement{DependencyRequirement::at_least};
};

namespace keels2
{

template <typename Value>
class ConVar;

}

namespace keels2::detail
{

template <typename Value>
inline constexpr bool kSupportedConVar =
    std::is_same_v<Value, bool> || std::is_same_v<Value, std::int32_t> ||
    std::is_same_v<Value, float> || std::is_same_v<Value, CUtlString>;

template <typename Value>
inline constexpr bool kBoundedConVar =
    std::is_same_v<Value, std::int32_t> || std::is_same_v<Value, float>;

template <typename Value>
constexpr KeelConVarType ConVarType() noexcept
{
    static_assert(kSupportedConVar<Value>);
    if constexpr (std::is_same_v<Value, bool>)
    {
        return KEELS2_CONVAR_BOOL;
    }
    else if constexpr (std::is_same_v<Value, std::int32_t>)
    {
        return KEELS2_CONVAR_INT32;
    }
    else if constexpr (std::is_same_v<Value, float>)
    {
        return KEELS2_CONVAR_FLOAT32;
    }
    else
    {
        return KEELS2_CONVAR_STRING;
    }
}

template <typename Value>
KeelConVarValue ToConVarValue(const Value& value) noexcept
{
    static_assert(kSupportedConVar<Value>);
    KeelConVarValue output{};
    output.size = sizeof(output);
    output.type = ConVarType<Value>();
    if constexpr (std::is_same_v<Value, bool>)
    {
        output.value.boolean_value = value ? KEEL_TRUE : KEEL_FALSE;
    }
    else if constexpr (std::is_same_v<Value, std::int32_t>)
    {
        output.value.int32_value = value;
    }
    else if constexpr (std::is_same_v<Value, float>)
    {
        output.value.float32_value = value;
    }
    else
    {
        const char* text = value.Get();
        output.value.string_value = text ? text : "";
    }
    return output;
}

inline bool EqualConVarName(std::string_view first, std::string_view second) noexcept
{
    if (first.size() != second.size())
    {
        return false;
    }
    for (std::size_t index{}; index < first.size(); ++index)
    {
        unsigned char left = static_cast<unsigned char>(first[index]);
        unsigned char right = static_cast<unsigned char>(second[index]);
        if (left >= 'A' && left <= 'Z')
        {
            left = static_cast<unsigned char>(left + ('a' - 'A'));
        }
        if (right >= 'A' && right <= 'Z')
        {
            right = static_cast<unsigned char>(right + ('a' - 'A'));
        }
        if (left != right)
        {
            return false;
        }
    }
    return true;
}

template <typename Value>
bool ValidConVarValue(const KeelConVarValue& value) noexcept
{
    static_assert(kSupportedConVar<Value>);
    if (value.size != sizeof(value) || value.type != ConVarType<Value>())
    {
        return false;
    }
    if constexpr (std::is_same_v<Value, bool>)
    {
        return value.value.boolean_value == KEEL_FALSE ||
            value.value.boolean_value == KEEL_TRUE;
    }
    else if constexpr (std::is_same_v<Value, float>)
    {
        return std::isfinite(value.value.float32_value);
    }
    else if constexpr (std::is_same_v<Value, CUtlString>)
    {
        return value.value.string_value != nullptr;
    }
    return true;
}

template <typename Value>
Value FromConVarValue(const KeelConVarValue& value)
{
    static_assert(kSupportedConVar<Value>);
    if (!ValidConVarValue<Value>(value))
    {
        return Value{};
    }
    if constexpr (std::is_same_v<Value, bool>)
    {
        return value.value.boolean_value == KEEL_TRUE;
    }
    else if constexpr (std::is_same_v<Value, std::int32_t>)
    {
        return value.value.int32_value;
    }
    else if constexpr (std::is_same_v<Value, float>)
    {
        return value.value.float32_value;
    }
    else
    {
        return CUtlString(value.value.string_value ? value.value.string_value : "");
    }
}

template <typename Value>
class AuthoringConVarState final
{
public:
    explicit AuthoringConVarState(std::string name)
        : name_(std::move(name))
    {
        static_assert(kSupportedConVar<Value>);
        static_assert(sizeof(ConVarRef) == 8);
        static_assert(std::is_trivially_copyable_v<ConVarRef>);
    }

    bool Bind(
        const KeelConVarApi* service,
        KeelPluginHandle plugin,
        KeelConVarHandle handle,
        void* native_convar) noexcept
    {
        if (!service || !service->read || !service->queue_set || !service->describe ||
            !plugin || !handle || !native_convar || !g_pCVar)
        {
            return false;
        }
        ConVarRef reference;
        std::memcpy(&reference, native_convar, sizeof(reference));
        try
        {
            CConVarRef<Value> native(reference);
            if (!native.IsConVarDataValid() ||
                native.GetType() != TranslateConVarType<Value>())
            {
                return false;
            }
            KeelConVarInfo info{};
            info.size = sizeof(info);
            if (service->describe(plugin, handle, &info) != KEEL_RESULT_OK ||
                info.size != sizeof(info) || info.type != ConVarType<Value>() ||
                !info.name || !EqualConVarName(name_, info.name) ||
                (info.has_minimum != KEEL_FALSE && info.has_minimum != KEEL_TRUE) ||
                (info.has_maximum != KEEL_FALSE && info.has_maximum != KEEL_TRUE) ||
                info.reserved_minimum != 0 || info.reserved_maximum != 0 ||
                !ValidConVarValue<Value>(info.default_value))
            {
                return false;
            }
            if constexpr (!kBoundedConVar<Value>)
            {
                if (info.has_minimum == KEEL_TRUE || info.has_maximum == KEEL_TRUE)
                {
                    return false;
                }
            }
            std::optional<Value> minimum;
            std::optional<Value> maximum;
            if (info.has_minimum == KEEL_TRUE)
            {
                if (!ValidConVarValue<Value>(info.minimum_value))
                {
                    return false;
                }
                minimum = FromConVarValue<Value>(info.minimum_value);
            }
            if (info.has_maximum == KEEL_TRUE)
            {
                if (!ValidConVarValue<Value>(info.maximum_value))
                {
                    return false;
                }
                maximum = FromConVarValue<Value>(info.maximum_value);
            }
            if constexpr (kBoundedConVar<Value>)
            {
                const Value default_value = FromConVarValue<Value>(info.default_value);
                if ((minimum && maximum && *minimum > *maximum) ||
                    (minimum && default_value < *minimum) ||
                    (maximum && default_value > *maximum))
                {
                    return false;
                }
            }
            name_ = info.name;
            native_convar_.emplace(native);
            minimum_ = std::move(minimum);
            maximum_ = std::move(maximum);
            service_ = service;
            plugin_ = plugin;
            handle_ = handle;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void SetActive(bool active) noexcept
    {
        active_.store(active, std::memory_order_release);
    }

    bool Active() const noexcept
    {
        return active_.load(std::memory_order_acquire);
    }

    Value Get() const
    {
        if (!Active() || !service_ || !service_->read)
        {
            return Value{};
        }
        KeelConVarValue value{};
        value.size = sizeof(value);
        value.type = ConVarType<Value>();
        return service_->read(plugin_, handle_, KEELS2_CONVAR_GLOBAL_SLOT, &value) ==
                KEEL_RESULT_OK
            ? FromConVarValue<Value>(value)
            : Value{};
    }

    bool Set(const Value& value) noexcept
    {
        try
        {
            if (!Active() || !service_ || !service_->queue_set)
            {
                return false;
            }
            const KeelConVarValue converted = ToConVarValue(value);
            if (!ValidConVarValue<Value>(converted))
            {
                return false;
            }
            return service_->queue_set(
                       plugin_,
                       handle_,
                       KEELS2_CONVAR_GLOBAL_SLOT,
                       &converted) == KEEL_RESULT_OK;
        }
        catch (...)
        {
            return false;
        }
    }

    const char* Name() const noexcept
    {
        return name_.c_str();
    }

    bool HasMinimum() const noexcept
    {
        return minimum_.has_value();
    }

    bool HasMaximum() const noexcept
    {
        return maximum_.has_value();
    }

    Value Minimum() const
    {
        if (minimum_)
        {
            return *minimum_;
        }
        if constexpr (kBoundedConVar<Value>)
        {
            return std::numeric_limits<Value>::lowest();
        }
        return Value{};
    }

    Value Maximum() const
    {
        if (maximum_)
        {
            return *maximum_;
        }
        if constexpr (kBoundedConVar<Value>)
        {
            return (std::numeric_limits<Value>::max)();
        }
        return Value{};
    }

    CConVarRef<Value>* Native() const noexcept
    {
        return Active() && native_convar_
            ? const_cast<CConVarRef<Value>*>(&*native_convar_)
            : nullptr;
    }

private:
    std::string name_;
    std::optional<CConVarRef<Value>> native_convar_;
    std::optional<Value> minimum_;
    std::optional<Value> maximum_;
    const KeelConVarApi* service_{};
    KeelPluginHandle plugin_{};
    KeelConVarHandle handle_{};
    std::atomic<bool> active_{};
};

template <typename Value>
class AuthoringTypedConVarResource;

}

namespace keels2
{

template <typename Value>
class ConVar final
{
public:
    static_assert(detail::kSupportedConVar<Value>);

    ConVar() = default;

    explicit operator bool() const noexcept
    {
        return state_ && state_->Active();
    }

    Value Get() const
    {
        return state_ ? state_->Get() : Value{};
    }

    bool Set(const Value& value) const noexcept
    {
        return state_ && state_->Set(value);
    }

    const char* GetName() const noexcept
    {
        return state_ ? state_->Name() : "";
    }

    bool HasMin() const noexcept
    {
        return state_ && state_->HasMinimum();
    }

    bool HasMax() const noexcept
    {
        return state_ && state_->HasMaximum();
    }

    Value Min() const
    {
        return state_ ? state_->Minimum() : Value{};
    }

    Value Max() const
    {
        return state_ ? state_->Maximum() : Value{};
    }

private:
    friend class Plugin;
    friend class detail::AuthoringTypedConVarResource<Value>;

    explicit ConVar(std::shared_ptr<detail::AuthoringConVarState<Value>> state) noexcept
        : state_(std::move(state))
    {
    }

    std::shared_ptr<detail::AuthoringConVarState<Value>> state_;
};

}

namespace keels2::detail
{

class AuthoringCommandBinding
{
public:
    AuthoringCommandBinding(Plugin& plugin, std::string name)
        : plugin_(plugin),
          name_(std::move(name))
    {
    }

    virtual ~AuthoringCommandBinding() = default;

    AuthoringCommandBinding(const AuthoringCommandBinding&) = delete;
    AuthoringCommandBinding& operator=(const AuthoringCommandBinding&) = delete;

    const std::string& Name() const noexcept
    {
        return name_;
    }

    void Adopt(
        std::shared_ptr<ContextState> state,
        const KeelSource2AuthoringApi* service,
        KeelCommandHandle handle) noexcept
    {
        state_ = std::move(state);
        service_ = service;
        handle_ = handle;
    }

    bool Active() const noexcept
    {
        return active_.load(std::memory_order_acquire);
    }

    KeelResult Reset() noexcept
    {
        active_.store(false, std::memory_order_release);
        if (!handle_)
        {
            return KEEL_RESULT_OK;
        }
        if (!state_ || !state_->accepting_resources.load(std::memory_order_acquire) ||
            !service_ || !service_->unregister_command)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        const KeelResult result = service_->unregister_command(state_->plugin, handle_);
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            Clear();
        }
        else if (result == KEEL_RESULT_BUSY)
        {
            active_.store(true, std::memory_order_release);
        }
        return result;
    }

    static void Dispatch(const void* context, const void* command, void* user_data) noexcept;

protected:
    virtual void Invoke(const CCommandContext& context, const CCommand& command) = 0;

private:
    void Clear() noexcept
    {
        state_.reset();
        service_ = nullptr;
        handle_ = 0;
    }

    Plugin& plugin_;
    std::string name_;
    std::shared_ptr<ContextState> state_;
    const KeelSource2AuthoringApi* service_{};
    KeelCommandHandle handle_{};
    std::atomic<bool> active_{true};
};

template <typename Owner>
class AuthoringMemberCommand final : public AuthoringCommandBinding
{
public:
    using Method = void (Owner::*)(const CCommandContext&, const CCommand&);

    AuthoringMemberCommand(
        Plugin& plugin,
        std::string name,
        Owner& owner,
        Method method)
        : AuthoringCommandBinding(plugin, std::move(name)),
          owner_(owner),
          method_(method)
    {
    }

private:
    void Invoke(const CCommandContext& context, const CCommand& command) override
    {
        std::invoke(method_, owner_, context, command);
    }

    Owner& owner_;
    Method method_;
};

class AuthoringConVarResource
{
public:
    explicit AuthoringConVarResource(Plugin& plugin)
        : plugin_(plugin)
    {
    }

    virtual ~AuthoringConVarResource() = default;

    AuthoringConVarResource(const AuthoringConVarResource&) = delete;
    AuthoringConVarResource& operator=(const AuthoringConVarResource&) = delete;

    bool Adopt(
        std::shared_ptr<ContextState> state,
        const KeelSource2AuthoringApi* service,
        const KeelConVarApi* convar_service,
        KeelConVarHandle handle,
        void* native_convar) noexcept
    {
        if (!state || !BindNative(
                convar_service,
                state->plugin,
                handle,
                native_convar))
        {
            return false;
        }
        state_ = std::move(state);
        service_ = service;
        handle_ = handle;
        SetHandleActive(true);
        active_.store(true, std::memory_order_release);
        return true;
    }

    KeelResult Reset() noexcept
    {
        const bool was_active = active_.exchange(false, std::memory_order_acq_rel);
        if (!handle_)
        {
            SetHandleActive(false);
            return KEEL_RESULT_OK;
        }
        if (!state_ || !state_->accepting_resources.load(std::memory_order_acquire) ||
            !service_ || !service_->release_convar)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        const KeelResult result = service_->release_convar(state_->plugin, handle_);
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            Clear();
        }
        else if (result == KEEL_RESULT_BUSY)
        {
            SetHandleActive(was_active);
            active_.store(was_active, std::memory_order_release);
        }
        else
        {
            SetHandleActive(false);
        }
        return result;
    }

    bool Active() const noexcept
    {
        return active_.load(std::memory_order_acquire);
    }

    virtual void* NativeConVar() const noexcept = 0;
    virtual const void* StateIdentity() const noexcept = 0;

    static void Dispatch(
        void* convar,
        std::int32_t slot,
        const void* new_value,
        const void* old_value,
        void* user_data) noexcept;

protected:
    virtual bool BindNative(
        const KeelConVarApi* service,
        KeelPluginHandle plugin,
        KeelConVarHandle handle,
        void* native_convar) noexcept = 0;
    virtual void SetHandleActive(bool active) noexcept = 0;

    virtual void Invoke(
        void* convar,
        std::int32_t slot,
        const void* new_value,
        const void* old_value)
    {
        static_cast<void>(convar);
        static_cast<void>(slot);
        static_cast<void>(new_value);
        static_cast<void>(old_value);
    }

private:
    void Clear() noexcept
    {
        SetHandleActive(false);
        state_.reset();
        service_ = nullptr;
        handle_ = 0;
    }

    Plugin& plugin_;
    std::shared_ptr<ContextState> state_;
    const KeelSource2AuthoringApi* service_{};
    KeelConVarHandle handle_{};
    std::atomic<bool> active_{};
};

template <typename Value>
class AuthoringTypedConVarResource : public AuthoringConVarResource
{
public:
    AuthoringTypedConVarResource(Plugin& plugin, std::string name)
        : AuthoringConVarResource(plugin),
          state_(std::make_shared<AuthoringConVarState<Value>>(std::move(name)))
    {
        static_assert(kSupportedConVar<Value>);
    }

    ~AuthoringTypedConVarResource() override
    {
        static_cast<void>(this->Reset());
    }

    void* NativeConVar() const noexcept override
    {
        return state_->Native();
    }

    const void* StateIdentity() const noexcept override
    {
        return state_.get();
    }

    keels2::ConVar<Value> Handle() const noexcept
    {
        return keels2::ConVar<Value>(state_);
    }

protected:
    bool BindNative(
        const KeelConVarApi* service,
        KeelPluginHandle plugin,
        KeelConVarHandle handle,
        void* native_convar) noexcept override
    {
        return state_->Bind(service, plugin, handle, native_convar);
    }

    void SetHandleActive(bool active) noexcept override
    {
        state_->SetActive(active);
    }

private:
    std::shared_ptr<AuthoringConVarState<Value>> state_;
};

class GameEventBinding
{
public:
    GameEventBinding(Plugin& plugin, std::string name)
        : plugin_(plugin), name_(std::move(name))
    {
    }

    virtual ~GameEventBinding() = default;

    GameEventBinding(const GameEventBinding&) = delete;
    GameEventBinding& operator=(const GameEventBinding&) = delete;

    const std::string& Name() const noexcept
    {
        return name_;
    }

    void Adopt(
        std::shared_ptr<ContextState> state,
        const KeelSource2CallbacksApi* service,
        KeelSource2SubscriptionHandle handle) noexcept
    {
        state_ = std::move(state);
        service_ = service;
        handle_ = handle;
    }

    bool Active() const noexcept
    {
        return active_.load(std::memory_order_acquire);
    }

    KeelResult Reset() noexcept
    {
        active_.store(false, std::memory_order_release);
        if (!handle_)
        {
            return KEEL_RESULT_OK;
        }
        if (!state_ || !state_->accepting_resources.load(std::memory_order_acquire) ||
            !service_ || !service_->unsubscribe)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        const KeelResult result = service_->unsubscribe(state_->plugin, handle_);
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            Clear();
        }
        else if (result == KEEL_RESULT_BUSY)
        {
            active_.store(true, std::memory_order_release);
        }
        return result;
    }

    static KeelBool Dispatch(
        const KeelSource2CallbackEvent* event,
        void* user_data) noexcept;

protected:
    virtual void Invoke(IGameEvent* event) = 0;

private:
    void Clear() noexcept
    {
        state_.reset();
        service_ = nullptr;
        handle_ = 0;
    }

    Plugin& plugin_;
    std::string name_;
    std::shared_ptr<ContextState> state_;
    const KeelSource2CallbacksApi* service_{};
    KeelSource2SubscriptionHandle handle_{};
    std::atomic<bool> active_{true};
};

template <typename Owner>
class MemberGameEvent final : public GameEventBinding
{
public:
    using Method = void (Owner::*)(IGameEvent*);

    MemberGameEvent(Plugin& plugin, std::string name, Owner& owner, Method method)
        : GameEventBinding(plugin, std::move(name)), owner_(owner), method_(method)
    {
    }

private:
    void Invoke(IGameEvent* event) override
    {
        std::invoke(method_, owner_, event);
    }

    Owner& owner_;
    Method method_;
};

template <typename Value, typename Owner>
class AuthoringMemberConVar final : public AuthoringTypedConVarResource<Value>
{
public:
    using Method = void (Owner::*)(
        keels2::ConVar<Value>&,
        CSplitScreenSlot,
        Value,
        Value);

    AuthoringMemberConVar(
        Plugin& plugin,
        std::string name,
        Owner& owner,
        Method method)
        : AuthoringTypedConVarResource<Value>(plugin, std::move(name)),
          owner_(owner),
          method_(method),
          convar_(this->Handle())
    {
    }

private:
    void Invoke(
        void* convar,
        std::int32_t slot,
        const void* new_value,
        const void* old_value) override
    {
        static_cast<void>(convar);
        std::invoke(
            method_,
            owner_,
            convar_,
            CSplitScreenSlot{slot},
            Value(*static_cast<const Value*>(new_value)),
            Value(*static_cast<const Value*>(old_value)));
    }

    Owner& owner_;
    Method method_;
    keels2::ConVar<Value> convar_;
};

}

namespace keels2
{

template <typename Value>
using SchemaField = schema::Field<Value>;

using Entity = entities::Entity;

class Plugin
{
public:
    template <typename Value>
    using ConVar = keels2::ConVar<Value>;

    virtual ~Plugin() = default;

    virtual bool Load()
    {
        return true;
    }
    virtual void Unload()
    {
    }

    virtual int32 CallbackPriority() const noexcept
    {
        return 0;
    }

    virtual void OnLevelInit(
        KeyValues* key_values,
        ILoopModePrerequisiteRegistry* prerequisite_registry)
    {
        static_cast<void>(key_values);
        static_cast<void>(prerequisite_registry);
    }

    virtual void OnLevelShutdown()
    {
    }

    virtual bool OnClientConnect(
        CPlayerSlot slot,
        const char* name,
        uint64 xuid,
        const char* network_id,
        bool unknown,
        CBufferString* rejection_message)
    {
        static_cast<void>(slot);
        static_cast<void>(name);
        static_cast<void>(xuid);
        static_cast<void>(network_id);
        static_cast<void>(unknown);
        static_cast<void>(rejection_message);
        return true;
    }

    virtual bool OnClientCommand(CPlayerSlot slot, const CCommand& command)
    {
        static_cast<void>(slot);
        static_cast<void>(command);
        return true;
    }

    virtual void OnGameFrame(bool simulating, bool first_tick, bool last_tick)
    {
        static_cast<void>(simulating);
        static_cast<void>(first_tick);
        static_cast<void>(last_tick);
    }

    virtual void OnClientConnected(
        CPlayerSlot slot,
        const char* name,
        uint64 xuid,
        const char* network_id,
        const char* address,
        bool fake_player)
    {
        static_cast<void>(slot);
        static_cast<void>(name);
        static_cast<void>(xuid);
        static_cast<void>(network_id);
        static_cast<void>(address);
        static_cast<void>(fake_player);
    }

    virtual void OnClientPutInServer(
        CPlayerSlot slot,
        const char* name,
        int client_type,
        uint64 xuid)
    {
        static_cast<void>(slot);
        static_cast<void>(name);
        static_cast<void>(client_type);
        static_cast<void>(xuid);
    }

    virtual void OnClientActive(
        CPlayerSlot slot,
        bool load_game,
        const char* name,
        uint64 xuid)
    {
        static_cast<void>(slot);
        static_cast<void>(load_game);
        static_cast<void>(name);
        static_cast<void>(xuid);
    }

    virtual void OnClientFullyConnected(CPlayerSlot slot)
    {
        static_cast<void>(slot);
    }

    virtual void OnClientDisconnecting(
        CPlayerSlot slot,
        ENetworkDisconnectionReason reason,
        const char* name,
        uint64 xuid,
        const char* network_id)
    {
        static_cast<void>(slot);
        static_cast<void>(reason);
        static_cast<void>(name);
        static_cast<void>(xuid);
        static_cast<void>(network_id);
    }

    virtual void OnClientSettingsChanged(CPlayerSlot slot)
    {
        static_cast<void>(slot);
    }

    virtual std::vector<PluginDependency> Dependencies() const
    {
        return {};
    }

    virtual void OnPluginLoaded(const PluginSnapshot& plugin)
    {
        static_cast<void>(plugin);
    }

    virtual void OnPluginUnloaded(const PluginSnapshot& plugin)
    {
        static_cast<void>(plugin);
    }

    virtual void OnPluginPaused(const PluginSnapshot& plugin)
    {
        static_cast<void>(plugin);
    }

    virtual void OnPluginResumed(const PluginSnapshot& plugin)
    {
        static_cast<void>(plugin);
    }

    virtual void OnAllPluginsLoaded()
    {
    }

protected:
    template <typename Owner>
    bool ListenForGameEvent(
        const char* name,
        void (Owner::*callback)(IGameEvent*))
    {
        static_assert(std::is_base_of_v<Plugin, Owner>);
        if (!name || !name[0] || !callback || !context_)
        {
            return false;
        }
        Owner* owner = dynamic_cast<Owner*>(this);
        if (!owner)
        {
            return false;
        }
        std::scoped_lock lock(game_events_mutex_);
        for (const auto& binding : game_events_)
        {
            if (binding->Active() && binding->Name() == name)
            {
                return false;
            }
        }
        const KeelSource2CallbacksApi* service = Source2CallbacksService();
        if (!service)
        {
            return false;
        }
        auto binding = std::make_unique<keels2::detail::MemberGameEvent<Owner>>(
            *this,
            name,
            *owner,
            callback);
        const KeelSource2SubscriptionSpec spec{
            sizeof(KeelSource2SubscriptionSpec),
            KEELS2_SOURCE2_GAME_EVENT,
            CallbackPriority(),
            0,
            name,
            &keels2::detail::GameEventBinding::Dispatch,
            binding.get()
        };
        KeelSource2SubscriptionHandle handle{};
        if (service->subscribe(context_.PluginHandle(), &spec, &handle) != KEEL_RESULT_OK ||
            !handle)
        {
            return false;
        }
        binding->Adopt(context_.State(), service, handle);
        game_events_.push_back(std::move(binding));
        return true;
    }

    bool StopListeningForGameEvent(const char* name) noexcept
    {
        if (!name)
        {
            return false;
        }
        std::scoped_lock lock(game_events_mutex_);
        for (auto& binding : game_events_)
        {
            if (!binding->Active() || binding->Name() != name)
            {
                continue;
            }
            const KeelResult result = binding->Reset();
            return result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND;
        }
        return false;
    }

    void LogMessage(const char* message) const noexcept
    {
        context_.Log(KEEL_LOG_INFO, message);
    }

    template <typename... Arguments>
        requires (sizeof...(Arguments) > 0)
    void LogMessage(const char* format, Arguments&&... arguments) const noexcept
    {
        context_.Log(
            KEEL_LOG_INFO,
            format,
            std::forward<Arguments>(arguments)...);
    }

    void LogWarning(const char* message) const noexcept
    {
        context_.Log(KEEL_LOG_WARNING, message);
    }

    template <typename... Arguments>
        requires (sizeof...(Arguments) > 0)
    void LogWarning(const char* format, Arguments&&... arguments) const noexcept
    {
        context_.Log(
            KEEL_LOG_WARNING,
            format,
            std::forward<Arguments>(arguments)...);
    }

    void LogError(const char* message) const noexcept
    {
        context_.Log(KEEL_LOG_ERROR, message);
    }

    template <typename... Arguments>
        requires (sizeof...(Arguments) > 0)
    void LogError(const char* format, Arguments&&... arguments) const noexcept
    {
        context_.Log(
            KEEL_LOG_ERROR,
            format,
            std::forward<Arguments>(arguments)...);
    }

    template <typename Owner>
    bool CreateCommand(
        const char* name,
        const char* description,
        void (Owner::*callback)(const CCommandContext&, const CCommand&),
        uint64 flags = 0)
    {
        static_assert(std::is_base_of_v<Plugin, Owner>);
        if (!name || !name[0] || !description || !callback || !context_)
        {
            return false;
        }
        Owner* owner = dynamic_cast<Owner*>(this);
        if (!owner)
        {
            return false;
        }
        std::scoped_lock lock(commands_mutex_);
        for (const auto& binding : commands_)
        {
            if (binding->Active() && binding->Name() == name)
            {
                return false;
            }
        }
        auto binding = std::make_unique<keels2::detail::AuthoringMemberCommand<Owner>>(
            *this,
            name,
            *owner,
            callback);
        const KeelSource2AuthoringApi* service = Source2AuthoringService();
        if (!service)
        {
            return false;
        }
        const KeelSource2CommandSpec spec{
            sizeof(KeelSource2CommandSpec),
            0,
            name,
            description,
            flags,
            &keels2::detail::AuthoringCommandBinding::Dispatch,
            binding.get()
        };
        KeelCommandHandle handle{};
        if (service->register_command(
                context_.PluginHandle(),
                &spec,
                &handle) != KEEL_RESULT_OK || !handle)
        {
            return false;
        }
        binding->Adopt(context_.State(), service, handle);
        commands_.push_back(std::move(binding));
        return true;
    }

    bool RemoveCommand(const char* name) noexcept
    {
        if (!name)
        {
            return false;
        }
        std::scoped_lock lock(commands_mutex_);
        for (auto& binding : commands_)
        {
            if (!binding->Active() || binding->Name() != name)
            {
                continue;
            }
            const KeelResult result = binding->Reset();
            if (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND)
            {
                return false;
            }
            return true;
        }
        return false;
    }

    template <typename Value>
    ConVar<Value> CreateConVar(
        const char* name,
        const Value& default_value,
        const char* help_string,
        uint64 flags = FCVAR_NONE)
    {
        static_assert(keels2::detail::kSupportedConVar<Value>);
        if (!name || !name[0])
        {
            return {};
        }
        auto resource =
            std::make_unique<keels2::detail::AuthoringTypedConVarResource<Value>>(
                *this,
                name);
        return CreateConVarResource<Value>(
            name,
            flags,
            help_string,
            default_value,
            false,
            Value{},
            false,
            Value{},
            std::move(resource),
            false);
    }

    template <typename Value>
    ConVar<Value> CreateConVar(
        const char* name,
        const Value& default_value,
        const char* help_string,
        uint64 flags,
        const Value& minimum,
        const Value& maximum)
    {
        static_assert(keels2::detail::kBoundedConVar<Value>);
        if (!name || !name[0])
        {
            return {};
        }
        auto resource =
            std::make_unique<keels2::detail::AuthoringTypedConVarResource<Value>>(
                *this,
                name);
        return CreateConVarResource<Value>(
            name,
            flags,
            help_string,
            default_value,
            true,
            minimum,
            true,
            maximum,
            std::move(resource),
            false);
    }

    template <typename Value, typename Owner>
    ConVar<Value> CreateConVar(
        const char* name,
        const Value& default_value,
        const char* help_string,
        uint64 flags,
        void (Owner::*callback)(
            ConVar<Value>&,
            CSplitScreenSlot,
            Value,
            Value))
    {
        static_assert(keels2::detail::kSupportedConVar<Value>);
        static_assert(std::is_base_of_v<Plugin, Owner>);
        if (!name || !name[0] || !callback)
        {
            return {};
        }
        Owner* owner = dynamic_cast<Owner*>(this);
        if (!owner)
        {
            return {};
        }
        auto resource = std::make_unique<keels2::detail::AuthoringMemberConVar<Value, Owner>>(
            *this,
            name,
            *owner,
            callback);
        return CreateConVarResource<Value>(
            name,
            flags,
            help_string,
            default_value,
            false,
            Value{},
            false,
            Value{},
            std::move(resource),
            true);
    }

    template <typename Value, typename Owner>
    ConVar<Value> CreateConVar(
        const char* name,
        const Value& default_value,
        const char* help_string,
        uint64 flags,
        const Value& minimum,
        const Value& maximum,
        void (Owner::*callback)(
            ConVar<Value>&,
            CSplitScreenSlot,
            Value,
            Value))
    {
        static_assert(keels2::detail::kBoundedConVar<Value>);
        static_assert(std::is_base_of_v<Plugin, Owner>);
        if (!name || !name[0] || !callback)
        {
            return {};
        }
        Owner* owner = dynamic_cast<Owner*>(this);
        if (!owner)
        {
            return {};
        }
        auto resource = std::make_unique<keels2::detail::AuthoringMemberConVar<Value, Owner>>(
            *this,
            name,
            *owner,
            callback);
        return CreateConVarResource<Value>(
            name,
            flags,
            help_string,
            default_value,
            true,
            minimum,
            true,
            maximum,
            std::move(resource),
            true);
    }

    template <typename Value>
    ConVar<Value> FindConVar(const char* name)
    {
        static_assert(keels2::detail::kSupportedConVar<Value>);
        if (!name || !name[0] || !context_)
        {
            return {};
        }
        std::scoped_lock lock(convars_mutex_);
        const KeelSource2AuthoringApi* service = Source2AuthoringService();
        const KeelConVarApi* convar_service = ConVarService();
        if (!service || !convar_service)
        {
            return {};
        }
        auto resource =
            std::make_unique<keels2::detail::AuthoringTypedConVarResource<Value>>(
                *this,
                name);
        KeelConVarHandle handle{};
        void* native_convar{};
        if (service->find_convar(
                context_.PluginHandle(),
                name,
                keels2::detail::ConVarType<Value>(),
                &handle,
                &native_convar) != KEEL_RESULT_OK || !handle || !native_convar)
        {
            return {};
        }
        g_pCVar = GetCVarSystem<ICvar>();
        if (!g_pCVar || !resource->Adopt(
                context_.State(),
                service,
                convar_service,
                handle,
                native_convar))
        {
            static_cast<void>(service->release_convar(context_.PluginHandle(), handle));
            return {};
        }
        ConVar<Value> output = resource->Handle();
        convar_resources_.push_back(std::move(resource));
        return output;
    }

    template <typename Value>
    bool RemoveConVar(const ConVar<Value>& convar) noexcept
    {
        static_assert(keels2::detail::kSupportedConVar<Value>);
        if (!convar.state_)
        {
            return false;
        }
        std::list<std::unique_ptr<keels2::detail::AuthoringConVarResource>> selected;
        {
            std::scoped_lock lock(convars_mutex_);
            for (auto resource = convar_resources_.begin();
                 resource != convar_resources_.end();
                 ++resource)
            {
                if (!(*resource)->Active() ||
                    (*resource)->StateIdentity() != convar.state_.get())
                {
                    continue;
                }
                selected.splice(selected.end(), convar_resources_, resource);
                break;
            }
        }
        if (selected.empty())
        {
            return false;
        }
        const KeelResult result = selected.front()->Reset();
        if (result == KEEL_RESULT_BUSY ||
            (result != KEEL_RESULT_OK && result != KEEL_RESULT_NOT_FOUND &&
                result != KEEL_RESULT_NOT_READY))
        {
            std::scoped_lock lock(convars_mutex_);
            convar_resources_.splice(convar_resources_.end(), selected);
        }
        return result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND;
    }

    std::vector<PluginSnapshot> Plugins()
    {
        std::vector<PluginSnapshot> snapshots;
        const KeelPluginsApi* service = PluginRuntimeService();
        if (!service || !context_)
        {
            return snapshots;
        }
        std::uint32_t count{};
        if (service->count(context_.PluginHandle(), &count) != KEEL_RESULT_OK)
        {
            return snapshots;
        }
        snapshots.reserve(count);
        for (std::uint32_t index{}; index < count; ++index)
        {
            KeelPluginSnapshot raw{};
            raw.size = sizeof(raw);
            PluginSnapshot snapshot;
            if (service->at(context_.PluginHandle(), index, &raw) != KEEL_RESULT_OK ||
                !CopyPluginSnapshot(raw, snapshot))
            {
                snapshots.clear();
                return snapshots;
            }
            snapshots.push_back(std::move(snapshot));
        }
        return snapshots;
    }

    std::optional<PluginSnapshot> FindPlugin(const char* name)
    {
        const KeelPluginsApi* service = PluginRuntimeService();
        if (!service || !context_ || !name || !name[0])
        {
            return std::nullopt;
        }
        KeelPluginSnapshot raw{};
        raw.size = sizeof(raw);
        PluginSnapshot snapshot;
        return service->find(context_.PluginHandle(), name, &raw) == KEEL_RESULT_OK &&
                CopyPluginSnapshot(raw, snapshot)
            ? std::optional<PluginSnapshot>(std::move(snapshot))
            : std::nullopt;
    }

    bool PausePlugin(PluginId plugin)
    {
        const KeelPluginsApi* service = PluginRuntimeService();
        return service && context_ && plugin &&
            service->pause(context_.PluginHandle(), plugin) == KEEL_RESULT_OK;
    }

    bool ResumePlugin(PluginId plugin)
    {
        const KeelPluginsApi* service = PluginRuntimeService();
        return service && context_ && plugin &&
            service->resume(context_.PluginHandle(), plugin) == KEEL_RESULT_OK;
    }

    template <typename Type>
    Type* GetSource2Server() noexcept
    {
        return Source2Interface<Type>(
            keels2::source2::Capability::server,
            keels2::source2::Factory::server,
            source2_server_);
    }

    template <typename Type>
    Type* GetSource2GameClients() noexcept
    {
        return Source2Interface<Type>(
            keels2::source2::Capability::game_clients,
            keels2::source2::Factory::server,
            source2_game_clients_);
    }

    template <typename Type>
    Type* GetCVarSystem() noexcept
    {
        return Source2Interface<Type>(
            keels2::source2::Capability::cvar,
            keels2::source2::Factory::engine,
            source2_cvar_);
    }

    template <typename Type>
    Type* GetEngineInterface(const char* interface_name) noexcept
    {
        return Source2Interface<Type>(
            keels2::source2::Factory::engine,
            interface_name);
    }

    template <typename Type>
    Type* GetServerInterface(const char* interface_name) noexcept
    {
        return Source2Interface<Type>(
            keels2::source2::Factory::server,
            interface_name);
    }

    template <typename TargetMethod, typename CallbackMethod>
    bool HookPre(
        keels2::kh::MethodClassOf<TargetMethod>* instance,
        TargetMethod target_method,
        CallbackMethod callback_method,
        int32 priority = 0)
    {
        return RegisterHook(
            instance,
            target_method,
            callback_method,
            keels2::kh::Phase::Pre,
            priority);
    }

    template <typename TargetMethod, typename CallbackMethod>
    bool HookPost(
        keels2::kh::MethodClassOf<TargetMethod>* instance,
        TargetMethod target_method,
        CallbackMethod callback_method,
        int32 priority = 0)
    {
        return RegisterHook(
            instance,
            target_method,
            callback_method,
            keels2::kh::Phase::Post,
            priority);
    }

    template <typename Signature, typename CallbackMethod>
    bool HookProfilePre(
        const char* target_name,
        CallbackMethod callback_method,
        int32 priority = 0)
    {
        return RegisterProfileHook<Signature>(
            target_name,
            callback_method,
            keels2::kh::Phase::Pre,
            priority);
    }

    template <typename Signature, typename CallbackMethod>
    bool HookProfilePost(
        const char* target_name,
        CallbackMethod callback_method,
        int32 priority = 0)
    {
        return RegisterProfileHook<Signature>(
            target_name,
            callback_method,
            keels2::kh::Phase::Post,
            priority);
    }

    template <typename Value>
    bool FindSchemaField(
        const char* class_name,
        const char* field_name,
        SchemaField<Value>& output) noexcept
    {
        static_assert(schema::detail::kSupportedValue<Value>);
        std::scoped_lock lock(schema_entities_mutex_);
        if (!context_ ||
            (!schema_service_ && schema_service_.Connect(context_) != KEEL_RESULT_OK))
        {
            return false;
        }
        return schema_service_.Resolve(class_name, field_name, output) == KEEL_RESULT_OK;
    }

    bool FindEntity(int index, Entity& output) noexcept
    {
        std::scoped_lock lock(schema_entities_mutex_);
        if (!context_ ||
            (!entities_service_ && entities_service_.Connect(context_) != KEEL_RESULT_OK))
        {
            return false;
        }
        return entities_service_.Find(index, output) == KEEL_RESULT_OK;
    }

    bool FindEntity(const CEntityHandle& handle, Entity& output) noexcept
    {
        std::scoped_lock lock(schema_entities_mutex_);
        if (!context_ ||
            (!entities_service_ && entities_service_.Connect(context_) != KEEL_RESULT_OK))
        {
            return false;
        }
        return entities_service_.FindSource2(
                   static_cast<uint32>(handle.ToInt()),
                   output) == KEEL_RESULT_OK;
    }

private:
    friend class keels2::detail::AuthoringCommandBinding;
    friend class keels2::detail::AuthoringConVarResource;
    friend class keels2::detail::GameEventBinding;
    template <typename Type>
    friend class keels2::detail::AuthoringAdapter;

    template <typename Type>
    Type* Source2Interface(
        keels2::source2::Capability capability,
        keels2::source2::Factory factory,
        keels2::source2::Interface& interface) noexcept
    {
        std::scoped_lock lock(source2_mutex_);
        if (!context_)
        {
            return nullptr;
        }
        if (!source2_connected_)
        {
            if (source2_service_.Connect(context_) != KEEL_RESULT_OK)
            {
                return nullptr;
            }
            source2_connected_ = true;
        }
        if (!interface && source2_service_.Query(capability, interface) != KEEL_RESULT_OK)
        {
            return nullptr;
        }
        if (interface.Type() != capability || interface.Origin() != factory)
        {
            interface.Reset();
            return nullptr;
        }
        return interface.template Get<Type>();
    }

    template <typename TargetMethod, typename CallbackMethod>
    bool RegisterHook(
        keels2::kh::MethodClassOf<TargetMethod>* instance,
        TargetMethod target_method,
        CallbackMethod callback_method,
        keels2::kh::Phase phase,
        int32 priority)
    {
        static_assert(std::is_member_function_pointer_v<TargetMethod>);
        static_assert(std::is_member_function_pointer_v<CallbackMethod>);
        static_assert(keels2::kh::CompatibleMethods<TargetMethod, CallbackMethod>);
        using Owner = keels2::kh::MethodClassOf<CallbackMethod>;
        static_assert(std::is_base_of_v<Plugin, Owner>);
        if (!hooks_accepting_.load(std::memory_order_acquire))
        {
            return false;
        }
        Owner* owner = dynamic_cast<Owner*>(this);
        const char* profile = Source2CompatibilityProfile();
        if (!instance || !owner || !profile)
        {
            return false;
        }
        try
        {
            std::scoped_lock lock(hooks_mutex_);
            if (!hooks_accepting_.load(std::memory_order_acquire))
            {
                return false;
            }
            hooks_.reserve(hooks_.size() + 1);
            keels2::kh::Service service;
            if (service.Connect(context_) != KEEL_RESULT_OK)
            {
                return false;
            }
            keels2::kh::Hook hook;
            if (service.AddVirtualHook(
                    instance,
                    target_method,
                    callback_method,
                    profile,
                    hook,
                    phase,
                    priority,
                    *owner) != KEEL_RESULT_OK)
            {
                return false;
            }
            hooks_.push_back(std::move(hook));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    template <typename Signature, typename CallbackMethod>
    bool RegisterProfileHook(
        const char* target_name,
        CallbackMethod callback_method,
        keels2::kh::Phase phase,
        int32 priority)
    {
        static_assert(std::is_member_function_pointer_v<CallbackMethod>);
        static_assert(keels2::kh::detail::CallbackCompatibility<
            Signature,
            keels2::kh::MethodSignatureOf<CallbackMethod>>::value);
        using Owner = keels2::kh::MethodClassOf<CallbackMethod>;
        static_assert(std::is_base_of_v<Plugin, Owner>);
        if (!target_name || !target_name[0] ||
            !hooks_accepting_.load(std::memory_order_acquire))
        {
            return false;
        }
        Owner* owner = dynamic_cast<Owner*>(this);
        if (!owner)
        {
            return false;
        }
        try
        {
            std::scoped_lock lock(hooks_mutex_);
            if (!hooks_accepting_.load(std::memory_order_acquire))
            {
                return false;
            }
            hooks_.reserve(hooks_.size() + 1);
            keels2::kh::Service service;
            if (service.Connect(context_) != KEEL_RESULT_OK)
            {
                return false;
            }
            keels2::kh::Hook hook;
            if (service.AddMethodHook<Signature>(
                    keels2::kh::TargetSpec::Profile(target_name),
                    callback_method,
                    hook,
                    phase,
                    priority,
                    *owner) != KEEL_RESULT_OK)
            {
                return false;
            }
            hooks_.push_back(std::move(hook));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    const char* Source2CompatibilityProfile() noexcept
    {
        std::scoped_lock lock(source2_mutex_);
        if (!context_)
        {
            return nullptr;
        }
        if (!source2_connected_)
        {
            if (source2_service_.Connect(context_) != KEEL_RESULT_OK)
            {
                return nullptr;
            }
            source2_connected_ = true;
        }
        if (!source2_server_ && source2_service_.Query(
                keels2::source2::Capability::server,
                source2_server_) != KEEL_RESULT_OK)
        {
            return nullptr;
        }
        const char* profile = source2_server_.CompatibilityProfile();
        return profile && profile[0] ? profile : nullptr;
    }

    template <typename Type>
    Type* Source2Interface(
        keels2::source2::Factory factory,
        const char* interface_name) noexcept
    {
        std::scoped_lock lock(source2_mutex_);
        if (!context_ || !interface_name || !interface_name[0])
        {
            return nullptr;
        }
        if (!source2_connected_)
        {
            if (source2_service_.Connect(context_) != KEEL_RESULT_OK)
            {
                return nullptr;
            }
            source2_connected_ = true;
        }
        keels2::source2::Interface interface;
        if (source2_service_.Query(factory, interface_name, interface) != KEEL_RESULT_OK ||
            interface.Origin() != factory)
        {
            return nullptr;
        }
        return interface.template Get<Type>();
    }

    template <typename Value>
    ConVar<Value> CreateConVarResource(
        const char* name,
        uint64 flags,
        const char* help_string,
        const Value& default_value,
        bool has_minimum,
        const Value& minimum,
        bool has_maximum,
        const Value& maximum,
        std::unique_ptr<keels2::detail::AuthoringTypedConVarResource<Value>> resource,
        bool callback)
    {
        if (!name || !name[0] || !help_string || !resource || !context_)
        {
            return {};
        }
        std::scoped_lock lock(convars_mutex_);
        const KeelSource2AuthoringApi* service = Source2AuthoringService();
        const KeelConVarApi* convar_service = ConVarService();
        if (!service || !convar_service)
        {
            return {};
        }
        KeelConVarSpec spec{};
        spec.size = sizeof(spec);
        spec.type = keels2::detail::ConVarType<Value>();
        spec.name = name;
        spec.description = help_string;
        spec.flags = flags;
        spec.default_value = keels2::detail::ToConVarValue(default_value);
        if (has_minimum)
        {
            spec.has_minimum = KEEL_TRUE;
            spec.minimum_value = keels2::detail::ToConVarValue(minimum);
        }
        if (has_maximum)
        {
            spec.has_maximum = KEEL_TRUE;
            spec.maximum_value = keels2::detail::ToConVarValue(maximum);
        }
        KeelConVarHandle handle{};
        void* native_convar{};
        if (service->create_convar(
                context_.PluginHandle(),
                &spec,
                callback ? &keels2::detail::AuthoringConVarResource::Dispatch : nullptr,
                callback ? resource.get() : nullptr,
                &handle,
                &native_convar) != KEEL_RESULT_OK || !handle || !native_convar)
        {
            return {};
        }
        g_pCVar = GetCVarSystem<ICvar>();
        if (!g_pCVar || !resource->Adopt(
                context_.State(),
                service,
                convar_service,
                handle,
                native_convar))
        {
            static_cast<void>(service->release_convar(context_.PluginHandle(), handle));
            return {};
        }
        ConVar<Value> output = resource->Handle();
        convar_resources_.push_back(std::move(resource));
        return output;
    }

    const KeelSource2AuthoringApi* Source2AuthoringService() noexcept
    {
        std::scoped_lock lock(source2_authoring_mutex_);
        if (source2_authoring_service_)
        {
            return source2_authoring_service_;
        }
        const void* raw{};
        if (context_.QueryService(
                KEELS2_SOURCE2_AUTHORING_SERVICE_NAME,
                KEELS2_SOURCE2_AUTHORING_API_VERSION,
                &raw) != KEEL_RESULT_OK)
        {
            return nullptr;
        }
        const auto* service = static_cast<const KeelSource2AuthoringApi*>(raw);
        if (!service || service->size != sizeof(KeelSource2AuthoringApi) ||
            service->api_version != KEELS2_SOURCE2_AUTHORING_API_VERSION ||
            !service->register_command || !service->unregister_command ||
            !service->create_convar || !service->find_convar ||
            !service->release_convar)
        {
            return nullptr;
        }
        source2_authoring_service_ = service;
        return source2_authoring_service_;
    }

    const KeelConVarApi* ConVarService() noexcept
    {
        std::scoped_lock lock(convar_service_mutex_);
        if (convar_service_)
        {
            return convar_service_;
        }
        const void* raw{};
        if (context_.QueryService(
                KEELS2_CONVAR_SERVICE_NAME,
                KEELS2_CONVAR_API_VERSION,
                &raw) != KEEL_RESULT_OK)
        {
            return nullptr;
        }
        const auto* service = static_cast<const KeelConVarApi*>(raw);
        if (!service || service->size != sizeof(KeelConVarApi) ||
            service->api_version != KEELS2_CONVAR_API_VERSION || !service->create ||
            !service->find || !service->release || !service->read ||
            !service->queue_set || !service->describe)
        {
            return nullptr;
        }
        convar_service_ = service;
        return convar_service_;
    }

    const KeelSource2CallbacksApi* Source2CallbacksService() noexcept
    {
        std::scoped_lock lock(source2_callbacks_mutex_);
        if (source2_callbacks_service_)
        {
            return source2_callbacks_service_;
        }
        const void* raw{};
        if (context_.QueryService(
                KEELS2_SOURCE2_CALLBACKS_SERVICE_NAME,
                KEELS2_SOURCE2_CALLBACKS_API_VERSION,
                &raw) != KEEL_RESULT_OK)
        {
            return nullptr;
        }
        const auto* service = static_cast<const KeelSource2CallbacksApi*>(raw);
        if (!service || service->size != sizeof(KeelSource2CallbacksApi) ||
            service->api_version != KEELS2_SOURCE2_CALLBACKS_API_VERSION ||
            !service->subscribe || !service->unsubscribe)
        {
            return nullptr;
        }
        source2_callbacks_service_ = service;
        return source2_callbacks_service_;
    }

    void CacheSource2CallbacksService(const KeelSource2CallbacksApi* service) noexcept
    {
        std::scoped_lock lock(source2_callbacks_mutex_);
        source2_callbacks_service_ = service;
    }

    static bool CopyPluginSnapshot(const KeelPluginSnapshot& raw, PluginSnapshot& output)
    {
        if (raw.size != sizeof(KeelPluginSnapshot) || !raw.handle ||
            raw.state > KEELS2_PLUGIN_STATE_ERROR)
        {
            return false;
        }
        const auto copy = [](const auto& source, std::string& destination) {
            const auto end = std::find(std::begin(source), std::end(source), '\0');
            if (end == std::end(source))
            {
                return false;
            }
            destination.assign(std::begin(source), end);
            return true;
        };
        PluginSnapshot snapshot;
        snapshot.id = raw.handle;
        snapshot.status = static_cast<PluginStatus>(raw.state);
        if (!copy(raw.name, snapshot.name) || !copy(raw.author, snapshot.author) ||
            !copy(raw.version, snapshot.version) ||
            !copy(raw.description, snapshot.description) || !copy(raw.file, snapshot.file) ||
            !copy(raw.diagnostic, snapshot.diagnostic))
        {
            return false;
        }
        output = std::move(snapshot);
        return true;
    }

    const KeelPluginsApi* PluginRuntimeService() noexcept
    {
        std::scoped_lock lock(plugin_service_mutex_);
        if (plugin_service_)
        {
            return plugin_service_;
        }
        const void* raw{};
        if (context_.QueryService(
                KEELS2_PLUGINS_SERVICE_NAME,
                KEELS2_PLUGINS_API_VERSION,
                &raw) != KEEL_RESULT_OK)
        {
            return nullptr;
        }
        const auto* service = static_cast<const KeelPluginsApi*>(raw);
        if (!service || service->size != sizeof(KeelPluginsApi) ||
            service->api_version != KEELS2_PLUGINS_API_VERSION || !service->count ||
            !service->at || !service->get || !service->find || !service->pause ||
            !service->resume || !service->subscribe || !service->unsubscribe)
        {
            return nullptr;
        }
        plugin_service_ = service;
        return plugin_service_;
    }

    void CachePluginRuntimeService(const KeelPluginsApi* service) noexcept
    {
        std::scoped_lock lock(plugin_service_mutex_);
        plugin_service_ = service;
    }

    bool DispatchEnabled() const noexcept
    {
        return dispatch_enabled_.load(std::memory_order_acquire);
    }

    void BeginHookRegistration() noexcept
    {
        std::scoped_lock lock(hooks_mutex_);
        hooks_accepting_.store(true, std::memory_order_release);
    }

    void ResetResources() noexcept
    {
        {
            std::vector<keels2::kh::Hook> hooks;
            {
                std::scoped_lock lock(hooks_mutex_);
                hooks_accepting_.store(false, std::memory_order_release);
                hooks.swap(hooks_);
            }
            for (auto& hook : hooks)
            {
                static_cast<void>(hook.Reset());
            }
        }
        {
            std::scoped_lock lock(game_events_mutex_);
            for (auto& binding : game_events_)
            {
                static_cast<void>(binding->Reset());
            }
            game_events_.clear();
        }
        {
            std::list<std::unique_ptr<keels2::detail::AuthoringConVarResource>> resources;
            {
                std::scoped_lock lock(convars_mutex_);
                resources.splice(resources.end(), convar_resources_);
            }
            for (auto& resource : resources)
            {
                static_cast<void>(resource->Reset());
            }
        }
        {
            std::scoped_lock lock(plugin_service_mutex_);
            plugin_service_ = nullptr;
        }
        {
            std::scoped_lock lock(commands_mutex_);
            for (auto& binding : commands_)
            {
                static_cast<void>(binding->Reset());
            }
            commands_.clear();
        }
        {
            std::scoped_lock lock(source2_authoring_mutex_);
            source2_authoring_service_ = nullptr;
        }
        {
            std::scoped_lock lock(convar_service_mutex_);
            convar_service_ = nullptr;
        }
        {
            std::scoped_lock lock(source2_callbacks_mutex_);
            source2_callbacks_service_ = nullptr;
        }
        {
            std::scoped_lock lock(source2_mutex_);
            source2_server_.Reset();
            source2_game_clients_.Reset();
            source2_cvar_.Reset();
            source2_service_ = {};
            source2_connected_ = false;
        }
        {
            std::scoped_lock lock(schema_entities_mutex_);
            schema_service_ = {};
            entities_service_ = {};
        }
    }

    std::vector<std::unique_ptr<keels2::detail::AuthoringCommandBinding>> commands_;
    std::list<std::unique_ptr<keels2::detail::AuthoringConVarResource>> convar_resources_;
    std::vector<std::unique_ptr<keels2::detail::GameEventBinding>> game_events_;
    std::vector<keels2::kh::Hook> hooks_;
    std::atomic<bool> hooks_accepting_{};
    std::mutex commands_mutex_;
    std::mutex convars_mutex_;
    std::mutex game_events_mutex_;
    std::mutex hooks_mutex_;
    std::mutex source2_authoring_mutex_;
    const KeelSource2AuthoringApi* source2_authoring_service_{};
    std::mutex convar_service_mutex_;
    const KeelConVarApi* convar_service_{};
    std::mutex source2_callbacks_mutex_;
    const KeelSource2CallbacksApi* source2_callbacks_service_{};
    std::mutex plugin_service_mutex_;
    const KeelPluginsApi* plugin_service_{};
    std::mutex source2_mutex_;
    keels2::source2::Service source2_service_;
    keels2::source2::Interface source2_server_;
    keels2::source2::Interface source2_game_clients_;
    keels2::source2::Interface source2_cvar_;
    std::atomic<bool> dispatch_enabled_{};
    bool source2_connected_{};
    std::mutex schema_entities_mutex_;
    schema::Service schema_service_;
    entities::Service entities_service_;
    keels2::Context context_;
};

}

inline void keels2::detail::AuthoringCommandBinding::Dispatch(
    const void* context,
    const void* command,
    void* user_data) noexcept
{
    auto* binding = static_cast<AuthoringCommandBinding*>(user_data);
    if (!binding || !binding->Active() || !binding->plugin_.DispatchEnabled() ||
        !context || !command)
    {
        return;
    }
    try
    {
        binding->Invoke(
            *static_cast<const CCommandContext*>(context),
            *static_cast<const CCommand*>(command));
    }
    catch (...)
    {
        binding->plugin_.LogError("exception escaped a command callback");
    }
}

inline void keels2::detail::AuthoringConVarResource::Dispatch(
    void* convar,
    std::int32_t slot,
    const void* new_value,
    const void* old_value,
    void* user_data) noexcept
{
    auto* resource = static_cast<AuthoringConVarResource*>(user_data);
    if (!resource || !resource->Active() || !resource->plugin_.DispatchEnabled() ||
        !convar || !new_value || !old_value)
    {
        return;
    }
    try
    {
        resource->Invoke(convar, slot, new_value, old_value);
    }
    catch (...)
    {
        resource->plugin_.LogError("exception escaped a ConVar callback");
    }
}

inline KeelBool keels2::detail::GameEventBinding::Dispatch(
    const KeelSource2CallbackEvent* event,
    void* user_data) noexcept
{
    auto* binding = static_cast<GameEventBinding*>(user_data);
    if (!binding || !binding->Active() || !binding->plugin_.DispatchEnabled() ||
        !event || event->size != sizeof(KeelSource2CallbackEvent) ||
        event->type != KEELS2_SOURCE2_GAME_EVENT ||
        event->payload_size != sizeof(KeelSource2GameEvent) || !event->payload)
    {
        return KEEL_TRUE;
    }
    const auto* payload = static_cast<const KeelSource2GameEvent*>(event->payload);
    if (payload->size != sizeof(KeelSource2GameEvent) || payload->reserved != 0 ||
        !payload->event)
    {
        return KEEL_TRUE;
    }
    try
    {
        binding->Invoke(static_cast<IGameEvent*>(payload->event));
    }
    catch (...)
    {
        binding->plugin_.LogError("exception escaped a game event callback");
    }
    return KEEL_TRUE;
}

namespace keels2::detail
{

template <typename Type>
class AuthoringAdapter final
{
    static_assert(std::is_base_of_v<::keels2::Plugin, Type>);
    static_assert(
        std::is_same_v<std::remove_cv_t<decltype(Type::Info)>, ::keels2::PluginInfo>);

    static constexpr bool kGameFrame =
        !std::is_same_v<decltype(&Type::OnGameFrame), decltype(&::keels2::Plugin::OnGameFrame)>;
    static constexpr bool kClientConnected =
        !std::is_same_v<decltype(&Type::OnClientConnected), decltype(&::keels2::Plugin::OnClientConnected)>;
    static constexpr bool kClientPutInServer =
        !std::is_same_v<decltype(&Type::OnClientPutInServer), decltype(&::keels2::Plugin::OnClientPutInServer)>;
    static constexpr bool kClientActive =
        !std::is_same_v<decltype(&Type::OnClientActive), decltype(&::keels2::Plugin::OnClientActive)>;
    static constexpr bool kClientFullyConnected =
        !std::is_same_v<decltype(&Type::OnClientFullyConnected), decltype(&::keels2::Plugin::OnClientFullyConnected)>;
    static constexpr bool kClientDisconnecting =
        !std::is_same_v<decltype(&Type::OnClientDisconnecting), decltype(&::keels2::Plugin::OnClientDisconnecting)>;
    static constexpr bool kClientSettingsChanged =
        !std::is_same_v<decltype(&Type::OnClientSettingsChanged), decltype(&::keels2::Plugin::OnClientSettingsChanged)>;
    static constexpr bool kPluginLoaded =
        !std::is_same_v<decltype(&Type::OnPluginLoaded), decltype(&::keels2::Plugin::OnPluginLoaded)>;
    static constexpr bool kPluginUnloaded =
        !std::is_same_v<decltype(&Type::OnPluginUnloaded), decltype(&::keels2::Plugin::OnPluginUnloaded)>;
    static constexpr bool kPluginPaused =
        !std::is_same_v<decltype(&Type::OnPluginPaused), decltype(&::keels2::Plugin::OnPluginPaused)>;
    static constexpr bool kPluginResumed =
        !std::is_same_v<decltype(&Type::OnPluginResumed), decltype(&::keels2::Plugin::OnPluginResumed)>;
    static constexpr bool kAllPluginsLoaded =
        !std::is_same_v<decltype(&Type::OnAllPluginsLoaded), decltype(&::keels2::Plugin::OnAllPluginsLoaded)>;
    static constexpr bool kLevelInit =
        !std::is_same_v<decltype(&Type::OnLevelInit), decltype(&::keels2::Plugin::OnLevelInit)>;
    static constexpr bool kLevelShutdown =
        !std::is_same_v<decltype(&Type::OnLevelShutdown), decltype(&::keels2::Plugin::OnLevelShutdown)>;
    static constexpr bool kClientConnect =
        !std::is_same_v<decltype(&Type::OnClientConnect), decltype(&::keels2::Plugin::OnClientConnect)>;
    static constexpr bool kClientCommand =
        !std::is_same_v<decltype(&Type::OnClientCommand), decltype(&::keels2::Plugin::OnClientCommand)>;
    static constexpr bool kHasLifecycle =
        kGameFrame || kClientConnected || kClientPutInServer || kClientActive ||
        kClientFullyConnected || kClientDisconnecting || kClientSettingsChanged;
    static constexpr bool kHasPluginEvents =
        kPluginLoaded || kPluginUnloaded || kPluginPaused || kPluginResumed ||
        kAllPluginsLoaded;
    static constexpr bool kHasSource2Callbacks =
        kLevelInit || kLevelShutdown || kClientConnect || kClientCommand;

    struct State final
    {
        struct CallbackState final
        {
            Type* instance{};
            KeelLifecycleEventType event{};
        };

        struct PluginCallbackState final
        {
            Type* instance{};
            KeelPluginEventType event{};
        };

        struct Source2CallbackState final
        {
            Type* instance{};
            KeelSource2CallbackType type{};
        };

        const KeelLifecycleApi* lifecycle{};
        std::array<KeelLifecycleSubscriptionHandle, 7> subscriptions{};
        std::array<CallbackState, 7> callbacks{};
        const KeelPluginsApi* plugins{};
        std::array<KeelPluginSubscriptionHandle, 5> plugin_subscriptions{};
        std::array<PluginCallbackState, 5> plugin_callbacks{};
        const KeelSource2CallbacksApi* source2_callbacks{};
        std::array<KeelSource2SubscriptionHandle, 6> source2_subscriptions{};
        std::array<Source2CallbackState, 6> source2_callback_states{};
        std::vector<std::string> dependency_names;
        std::vector<std::string> dependency_versions;
        std::vector<KeelPluginDependency> dependency_records;
        bool loaded{};
    };

public:
    static KeelBool Query(const KeelHostQuery* query, KeelPluginInfo* output) noexcept
    {
        if (!query || query->size != sizeof(KeelHostQuery) ||
            query->abi_version != KEELS2_PLUGIN_ABI_VERSION || !output ||
            output->size != sizeof(KeelPluginInfo))
        {
            return KEEL_FALSE;
        }
        try
        {
            constexpr const ::keels2::PluginInfo& info = Type::Info;
            if (!info.name || !info.name[0] || !info.author || !info.author[0] ||
                !info.version || !info.version[0] || !info.description ||
                !info.description[0])
            {
                return KEEL_FALSE;
            }
            *output = {
                sizeof(KeelPluginInfo),
                KEELS2_PLUGIN_ABI_VERSION,
                info.name,
                info.author,
                info.version,
                info.description
            };
            return KEEL_TRUE;
        }
        catch (...)
        {
            return KEEL_FALSE;
        }
    }

    static KeelBool Manifest(
        const KeelHostQuery* query,
        KeelPluginManifest* output) noexcept
    {
        if (!query || query->size != sizeof(KeelHostQuery) ||
            query->abi_version != KEELS2_PLUGIN_ABI_VERSION || !output ||
            output->size != sizeof(KeelPluginManifest))
        {
            return KEEL_FALSE;
        }
        try
        {
            State& state = PluginState();
            const auto dependencies = Instance().Dependencies();
            if (dependencies.size() > UINT32_MAX)
            {
                return KEEL_FALSE;
            }
            state.dependency_names.clear();
            state.dependency_versions.clear();
            state.dependency_records.clear();
            state.dependency_names.reserve(dependencies.size());
            state.dependency_versions.reserve(dependencies.size());
            for (const auto& dependency : dependencies)
            {
                state.dependency_names.push_back(dependency.name);
                state.dependency_versions.push_back(dependency.version);
            }
            state.dependency_records.reserve(dependencies.size());
            for (std::size_t index{}; index < dependencies.size(); ++index)
            {
                state.dependency_records.push_back({
                    sizeof(KeelPluginDependency),
                    static_cast<KeelPluginDependencyRequirement>(dependencies[index].requirement),
                    state.dependency_names[index].c_str(),
                    state.dependency_versions[index].c_str()
                });
            }
            *output = {
                sizeof(KeelPluginManifest),
                KEELS2_PLUGIN_MANIFEST_VERSION,
                static_cast<std::uint32_t>(state.dependency_records.size()),
                0,
                state.dependency_records.empty() ? nullptr : state.dependency_records.data()
            };
            return KEEL_TRUE;
        }
        catch (...)
        {
            return KEEL_FALSE;
        }
    }

    static KeelBool Load(const KeelHostApi* api, KeelPluginHandle plugin) noexcept
    {
        if (!ValidHost(api, plugin))
        {
            return KEEL_FALSE;
        }
        Type* instance{};
        State* state{};
        try
        {
            instance = &Instance();
            state = &PluginState();
            if (instance->context_ || state->loaded)
            {
                return KEEL_FALSE;
            }
            instance->context_.Bind(api, plugin);
            instance->dispatch_enabled_.store(false, std::memory_order_release);
            if (!PrepareLifecycle(*instance, *state) ||
                !PrepareSource2Callbacks(*instance, *state) ||
                !PreparePluginEvents(*instance, *state))
            {
                Rollback(*instance, *state);
                return KEEL_FALSE;
            }
            instance->BeginHookRegistration();
            if (!instance->Load())
            {
                Rollback(*instance, *state);
                return KEEL_FALSE;
            }
            state->loaded = true;
            instance->dispatch_enabled_.store(true, std::memory_order_release);
            return KEEL_TRUE;
        }
        catch (...)
        {
            if (instance && state)
            {
                Rollback(*instance, *state);
            }
            return KEEL_FALSE;
        }
    }

    static void Unload(KeelPluginHandle plugin) noexcept
    {
        try
        {
            Type& instance = Instance();
            State& state = PluginState();
            if (!instance.context_ || instance.context_.PluginHandle() != plugin)
            {
                return;
            }
            instance.dispatch_enabled_.store(false, std::memory_order_release);
            ReleaseLifecycle(instance, state);
            ReleaseSource2Callbacks(instance, state);
            ReleasePluginEvents(instance, state);
            instance.ResetResources();
            instance.context_.DisableResources();
            if (std::exchange(state.loaded, false))
            {
                try
                {
                    instance.Unload();
                }
                catch (...)
                {
                    instance.LogError("exception escaped the unload callback");
                }
            }
            state.lifecycle = nullptr;
            state.source2_callbacks = nullptr;
            state.plugins = nullptr;
            instance.context_.Unbind();
        }
        catch (...)
        {
        }
    }

private:
    static bool ValidHost(const KeelHostApi* api, KeelPluginHandle plugin) noexcept
    {
        return api && api->size == sizeof(KeelHostApi) &&
            api->abi_version == KEELS2_PLUGIN_ABI_VERSION && api->log &&
            api->register_command && api->unregister_command && api->query_service && plugin;
    }

    static Type& Instance()
    {
        static Type instance;
        return instance;
    }

    static State& PluginState() noexcept
    {
        static State state;
        return state;
    }

    static bool PrepareLifecycle(Type& instance, State& state) noexcept
    {
        if constexpr (!kHasLifecycle)
        {
            return true;
        }
        const void* raw{};
        if (instance.context_.QueryService(
                KEELS2_LIFECYCLE_SERVICE_NAME,
                KEELS2_LIFECYCLE_API_VERSION,
                &raw) != KEEL_RESULT_OK)
        {
            return false;
        }
        const auto* lifecycle = static_cast<const KeelLifecycleApi*>(raw);
        if (!lifecycle || lifecycle->size != sizeof(KeelLifecycleApi) ||
            lifecycle->api_version != KEELS2_LIFECYCLE_API_VERSION ||
            !lifecycle->subscribe || !lifecycle->unsubscribe)
        {
            return false;
        }
        state.lifecycle = lifecycle;
        return SubscribeAll(instance, state);
    }

    static bool PrepareSource2Callbacks(Type& instance, State& state) noexcept
    {
        if constexpr (!kHasSource2Callbacks)
        {
            return true;
        }
        const void* raw{};
        if (instance.context_.QueryService(
                KEELS2_SOURCE2_CALLBACKS_SERVICE_NAME,
                KEELS2_SOURCE2_CALLBACKS_API_VERSION,
                &raw) != KEEL_RESULT_OK)
        {
            return false;
        }
        const auto* callbacks = static_cast<const KeelSource2CallbacksApi*>(raw);
        if (!callbacks || callbacks->size != sizeof(KeelSource2CallbacksApi) ||
            callbacks->api_version != KEELS2_SOURCE2_CALLBACKS_API_VERSION ||
            !callbacks->subscribe || !callbacks->unsubscribe)
        {
            return false;
        }
        state.source2_callbacks = callbacks;
        instance.CacheSource2CallbacksService(callbacks);
        return SubscribeSource2<kLevelInit>(instance, state, KEELS2_SOURCE2_LEVEL_INIT) &&
            SubscribeSource2<kLevelShutdown>(
                instance,
                state,
                KEELS2_SOURCE2_LEVEL_SHUTDOWN) &&
            SubscribeSource2<kClientConnect>(
                instance,
                state,
                KEELS2_SOURCE2_CLIENT_CONNECT) &&
            SubscribeSource2<kClientCommand>(
                instance,
                state,
                KEELS2_SOURCE2_CLIENT_COMMAND);
    }

    template <bool Enabled>
    static bool SubscribeSource2(
        Type& instance,
        State& state,
        KeelSource2CallbackType type) noexcept
    {
        if constexpr (!Enabled)
        {
            return true;
        }
        auto& callback = state.source2_callback_states[type];
        callback = {&instance, type};
        const KeelSource2SubscriptionSpec spec{
            sizeof(KeelSource2SubscriptionSpec),
            type,
            instance.CallbackPriority(),
            0,
            nullptr,
            &Source2Dispatch,
            &callback
        };
        return state.source2_callbacks->subscribe(
                   instance.context_.PluginHandle(),
                   &spec,
                   &state.source2_subscriptions[type]) == KEEL_RESULT_OK &&
            state.source2_subscriptions[type] != 0;
    }

    static void ReleaseSource2Callbacks(Type& instance, State& state) noexcept
    {
        if (state.source2_callbacks)
        {
            for (auto& subscription : state.source2_subscriptions)
            {
                if (subscription)
                {
                    static_cast<void>(state.source2_callbacks->unsubscribe(
                        instance.context_.PluginHandle(),
                        subscription));
                    subscription = 0;
                }
            }
        }
        state.source2_subscriptions = {};
        state.source2_callback_states = {};
        state.source2_callbacks = nullptr;
        instance.CacheSource2CallbacksService(nullptr);
    }

    static bool PreparePluginEvents(Type& instance, State& state) noexcept
    {
        if constexpr (!kHasPluginEvents)
        {
            return true;
        }
        const void* raw{};
        if (instance.context_.QueryService(
                KEELS2_PLUGINS_SERVICE_NAME,
                KEELS2_PLUGINS_API_VERSION,
                &raw) != KEEL_RESULT_OK)
        {
            return false;
        }
        const auto* plugins = static_cast<const KeelPluginsApi*>(raw);
        if (!plugins || plugins->size != sizeof(KeelPluginsApi) ||
            plugins->api_version != KEELS2_PLUGINS_API_VERSION || !plugins->count ||
            !plugins->at || !plugins->get || !plugins->find || !plugins->pause ||
            !plugins->resume || !plugins->subscribe || !plugins->unsubscribe)
        {
            return false;
        }
        state.plugins = plugins;
        instance.CachePluginRuntimeService(plugins);
        return SubscribeAllPluginEvents(instance, state);
    }

    static bool SubscribeAllPluginEvents(Type& instance, State& state) noexcept
    {
        return SubscribePluginEvent<kPluginLoaded>(
                   instance, state, KEELS2_PLUGIN_EVENT_LOADED) &&
            SubscribePluginEvent<kPluginUnloaded>(
                instance, state, KEELS2_PLUGIN_EVENT_UNLOADED) &&
            SubscribePluginEvent<kPluginPaused>(
                instance, state, KEELS2_PLUGIN_EVENT_PAUSED) &&
            SubscribePluginEvent<kPluginResumed>(
                instance, state, KEELS2_PLUGIN_EVENT_RESUMED) &&
            SubscribePluginEvent<kAllPluginsLoaded>(
                instance, state, KEELS2_PLUGIN_EVENT_ALL_LOADED);
    }

    template <bool Enabled>
    static bool SubscribePluginEvent(
        Type& instance,
        State& state,
        KeelPluginEventType event) noexcept
    {
        if constexpr (!Enabled)
        {
            return true;
        }
        KeelPluginSubscriptionSpec spec{
            sizeof(KeelPluginSubscriptionSpec),
            event,
            0,
            &PluginEventDispatch,
            &state.plugin_callbacks[event - 1]
        };
        state.plugin_callbacks[event - 1] = {&instance, event};
        return state.plugins->subscribe(
                   instance.context_.PluginHandle(),
                   &spec,
                   &state.plugin_subscriptions[event - 1]) == KEEL_RESULT_OK &&
            state.plugin_subscriptions[event - 1] != 0;
    }

    static void ReleasePluginEvents(Type& instance, State& state) noexcept
    {
        if (state.plugins)
        {
            for (auto& subscription : state.plugin_subscriptions)
            {
                if (subscription)
                {
                    static_cast<void>(state.plugins->unsubscribe(
                        instance.context_.PluginHandle(),
                        subscription));
                    subscription = 0;
                }
            }
        }
        state.plugin_subscriptions = {};
        state.plugin_callbacks = {};
        state.plugins = nullptr;
        instance.CachePluginRuntimeService(nullptr);
    }

    static bool SubscribeAll(Type& instance, State& state) noexcept
    {
        return Subscribe<kGameFrame>(instance, state, KEELS2_LIFECYCLE_GAME_FRAME) &&
            Subscribe<kClientConnected>(instance, state, KEELS2_LIFECYCLE_CLIENT_CONNECTED) &&
            Subscribe<kClientPutInServer>(instance, state, KEELS2_LIFECYCLE_CLIENT_PUT_IN_SERVER) &&
            Subscribe<kClientActive>(instance, state, KEELS2_LIFECYCLE_CLIENT_ACTIVE) &&
            Subscribe<kClientFullyConnected>(instance, state, KEELS2_LIFECYCLE_CLIENT_FULLY_CONNECTED) &&
            Subscribe<kClientDisconnecting>(instance, state, KEELS2_LIFECYCLE_CLIENT_DISCONNECTING) &&
            Subscribe<kClientSettingsChanged>(instance, state, KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED);
    }

    template <bool Enabled>
    static bool Subscribe(Type& instance, State& state, KeelLifecycleEventType event) noexcept
    {
        if constexpr (!Enabled)
        {
            return true;
        }
        KeelLifecycleSubscriptionSpec spec{
            sizeof(KeelLifecycleSubscriptionSpec),
            event,
            0,
            &LifecycleDispatch,
            &state.callbacks[event - 1]
        };
        state.callbacks[event - 1] = {&instance, event};
        return state.lifecycle->subscribe(
                   instance.context_.PluginHandle(),
                   &spec,
                   &state.subscriptions[event - 1]) == KEEL_RESULT_OK &&
            state.subscriptions[event - 1] != 0;
    }

    static void ReleaseLifecycle(Type& instance, State& state) noexcept
    {
        if (!state.lifecycle)
        {
            state.subscriptions = {};
            state.callbacks = {};
            return;
        }
        for (auto& subscription : state.subscriptions)
        {
            if (subscription)
            {
                static_cast<void>(state.lifecycle->unsubscribe(
                    instance.context_.PluginHandle(),
                    subscription));
                subscription = 0;
            }
        }
        state.callbacks = {};
    }

    static void Rollback(Type& instance, State& state) noexcept
    {
        instance.dispatch_enabled_.store(false, std::memory_order_release);
        ReleaseLifecycle(instance, state);
        ReleaseSource2Callbacks(instance, state);
        ReleasePluginEvents(instance, state);
        instance.ResetResources();
        state.loaded = false;
        state.lifecycle = nullptr;
        state.source2_callbacks = nullptr;
        state.plugins = nullptr;
        instance.context_.DisableResources();
        instance.context_.Unbind();
    }

    static void LifecycleDispatch(const KeelLifecycleEvent* event, void* user_data) noexcept
    {
        auto* callback = static_cast<typename State::CallbackState*>(user_data);
        Type* instance = callback ? callback->instance : nullptr;
        if (!instance || !instance->DispatchEnabled() || !ValidEnvelope(event) ||
            event->type != callback->event)
        {
            return;
        }
        try
        {
            DispatchEvent(*instance, *event);
        }
        catch (...)
        {
            instance->LogError("exception escaped a lifecycle callback");
        }
    }

    static KeelBool Source2Dispatch(
        const KeelSource2CallbackEvent* event,
        void* user_data) noexcept
    {
        auto* callback = static_cast<typename State::Source2CallbackState*>(user_data);
        Type* instance = callback ? callback->instance : nullptr;
        if (!instance || !instance->DispatchEnabled() || !event ||
            event->size != sizeof(KeelSource2CallbackEvent) ||
            event->type != callback->type || event->reserved != 0 || !event->payload)
        {
            return KEEL_TRUE;
        }
        try
        {
            switch (event->type)
            {
                case KEELS2_SOURCE2_LEVEL_INIT:
                {
                    if constexpr (kLevelInit)
                    {
                        const auto* payload = Source2Payload<KeelSource2LevelInit>(*event);
                        if (payload && payload->reserved == 0)
                        {
                            instance->OnLevelInit(
                                const_cast<KeyValues*>(
                                    static_cast<const KeyValues*>(payload->key_values)),
                                const_cast<ILoopModePrerequisiteRegistry*>(
                                    static_cast<const ILoopModePrerequisiteRegistry*>(
                                        payload->prerequisite_registry)));
                        }
                    }
                    break;
                }
                case KEELS2_SOURCE2_LEVEL_SHUTDOWN:
                {
                    if constexpr (kLevelShutdown)
                    {
                        const auto* payload = Source2Payload<KeelSource2LevelShutdown>(*event);
                        if (payload && payload->reserved == 0)
                        {
                            instance->OnLevelShutdown();
                        }
                    }
                    break;
                }
                case KEELS2_SOURCE2_CLIENT_CONNECT:
                {
                    if constexpr (kClientConnect)
                    {
                        const auto* payload = Source2Payload<KeelSource2ClientConnect>(*event);
                        if (!payload || !payload->name || !payload->network_id ||
                            !ValidBool(payload->unknown) || payload->reserved != 0 ||
                            payload->reserved_message != 0 || !payload->rejection_message ||
                            payload->rejection_capacity == 0)
                        {
                            return KEEL_TRUE;
                        }
                        CBufferStringN<KEELS2_SOURCE2_REJECTION_CAPACITY> rejection(false);
                        const bool accepted = instance->OnClientConnect(
                            CPlayerSlot{payload->slot},
                            payload->name,
                            payload->xuid,
                            payload->network_id,
                            payload->unknown != KEEL_FALSE,
                            &rejection);
                        if (!accepted)
                        {
                            const char* message = rejection.Get();
                            const std::size_t length = (std::min)(
                                std::strlen(message),
                                static_cast<std::size_t>(payload->rejection_capacity - 1));
                            std::memcpy(payload->rejection_message, message, length);
                            payload->rejection_message[length] = '\0';
                            return KEEL_FALSE;
                        }
                    }
                    break;
                }
                case KEELS2_SOURCE2_CLIENT_COMMAND:
                {
                    if constexpr (kClientCommand)
                    {
                        const auto* payload = Source2Payload<KeelSource2ClientCommand>(*event);
                        if (payload && payload->command &&
                            !instance->OnClientCommand(
                                CPlayerSlot{payload->slot},
                                *static_cast<const CCommand*>(payload->command)))
                        {
                            return KEEL_FALSE;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
        catch (...)
        {
            instance->LogError("exception escaped a Source 2 callback");
        }
        return KEEL_TRUE;
    }

    static void PluginEventDispatch(const KeelPluginEvent* event, void* user_data) noexcept
    {
        auto* callback = static_cast<typename State::PluginCallbackState*>(user_data);
        Type* instance = callback ? callback->instance : nullptr;
        if (!instance || !instance->DispatchEnabled() || !event ||
            event->size != sizeof(KeelPluginEvent) || event->type != callback->event ||
            event->sequence == 0 || event->plugin.size != sizeof(KeelPluginSnapshot))
        {
            return;
        }
        try
        {
            if (event->type == KEELS2_PLUGIN_EVENT_ALL_LOADED)
            {
                if constexpr (kAllPluginsLoaded)
                {
                    if (event->plugin.handle == 0 &&
                        event->plugin.state == KEELS2_PLUGIN_STATE_UNKNOWN)
                    {
                        instance->OnAllPluginsLoaded();
                    }
                }
                return;
            }
            PluginSnapshot snapshot;
            if (!::keels2::Plugin::CopyPluginSnapshot(event->plugin, snapshot))
            {
                return;
            }
            switch (event->type)
            {
                case KEELS2_PLUGIN_EVENT_LOADED:
                    if constexpr (kPluginLoaded)
                    {
                        instance->OnPluginLoaded(snapshot);
                    }
                    break;
                case KEELS2_PLUGIN_EVENT_UNLOADED:
                    if constexpr (kPluginUnloaded)
                    {
                        instance->OnPluginUnloaded(snapshot);
                    }
                    break;
                case KEELS2_PLUGIN_EVENT_PAUSED:
                    if constexpr (kPluginPaused)
                    {
                        instance->OnPluginPaused(snapshot);
                    }
                    break;
                case KEELS2_PLUGIN_EVENT_RESUMED:
                    if constexpr (kPluginResumed)
                    {
                        instance->OnPluginResumed(snapshot);
                    }
                    break;
                default:
                    break;
            }
        }
        catch (...)
        {
            instance->LogError("exception escaped a plugin event callback");
        }
    }

    static bool ValidEnvelope(const KeelLifecycleEvent* event) noexcept
    {
        return event && event->size == sizeof(KeelLifecycleEvent) &&
            event->reserved == 0 && event->payload && event->payload_size != 0;
    }

    template <typename Payload>
    static const Payload* PayloadFrom(const KeelLifecycleEvent& event) noexcept
    {
        if (event.payload_size != sizeof(Payload))
        {
            return nullptr;
        }
        const auto* payload = static_cast<const Payload*>(event.payload);
        return payload->size == sizeof(Payload) ? payload : nullptr;
    }

    template <typename Payload>
    static const Payload* Source2Payload(const KeelSource2CallbackEvent& event) noexcept
    {
        if (event.payload_size != sizeof(Payload))
        {
            return nullptr;
        }
        const auto* payload = static_cast<const Payload*>(event.payload);
        return payload->size == sizeof(Payload) ? payload : nullptr;
    }

    static bool ValidBool(KeelBool value) noexcept
    {
        return value == KEEL_FALSE || value == KEEL_TRUE;
    }

    static void DispatchEvent(Type& instance, const KeelLifecycleEvent& event)
    {
        switch (event.type)
        {
        case KEELS2_LIFECYCLE_GAME_FRAME:
            DispatchGameFrame(instance, event);
            break;
        case KEELS2_LIFECYCLE_CLIENT_CONNECTED:
            DispatchClientConnected(instance, event);
            break;
        case KEELS2_LIFECYCLE_CLIENT_PUT_IN_SERVER:
            DispatchClientPutInServer(instance, event);
            break;
        case KEELS2_LIFECYCLE_CLIENT_ACTIVE:
            DispatchClientActive(instance, event);
            break;
        case KEELS2_LIFECYCLE_CLIENT_FULLY_CONNECTED:
            DispatchClientFullyConnected(instance, event);
            break;
        case KEELS2_LIFECYCLE_CLIENT_DISCONNECTING:
            DispatchClientDisconnecting(instance, event);
            break;
        case KEELS2_LIFECYCLE_CLIENT_SETTINGS_CHANGED:
            DispatchClientSettingsChanged(instance, event);
            break;
        default:
            break;
        }
    }

    static void DispatchGameFrame(Type& instance, const KeelLifecycleEvent& event)
    {
        if constexpr (kGameFrame)
        {
            if (const auto* payload = PayloadFrom<KeelLifecycleGameFrame>(event);
                payload && ValidBool(payload->simulating) &&
                ValidBool(payload->first_tick) && ValidBool(payload->last_tick))
            {
                instance.OnGameFrame(
                    payload->simulating != KEEL_FALSE,
                    payload->first_tick != KEEL_FALSE,
                    payload->last_tick != KEEL_FALSE);
            }
        }
    }

    static void DispatchClientConnected(Type& instance, const KeelLifecycleEvent& event)
    {
        if constexpr (kClientConnected)
        {
            if (const auto* payload = PayloadFrom<KeelLifecycleClientConnected>(event);
                payload && payload->reserved == 0 && ValidBool(payload->fake_player))
            {
                instance.OnClientConnected(
                    CPlayerSlot{payload->slot},
                    payload->name,
                    payload->xuid,
                    payload->network_id,
                    payload->address,
                    payload->fake_player != KEEL_FALSE);
            }
        }
    }

    static void DispatchClientPutInServer(Type& instance, const KeelLifecycleEvent& event)
    {
        if constexpr (kClientPutInServer)
        {
            if (const auto* payload = PayloadFrom<KeelLifecycleClientPutInServer>(event);
                payload && payload->reserved == 0)
            {
                instance.OnClientPutInServer(
                    CPlayerSlot{payload->slot},
                    payload->name,
                    payload->client_type,
                    payload->xuid);
            }
        }
    }

    static void DispatchClientActive(Type& instance, const KeelLifecycleEvent& event)
    {
        if constexpr (kClientActive)
        {
            if (const auto* payload = PayloadFrom<KeelLifecycleClientActive>(event);
                payload && payload->reserved == 0 && ValidBool(payload->load_game))
            {
                instance.OnClientActive(
                    CPlayerSlot{payload->slot},
                    payload->load_game != KEEL_FALSE,
                    payload->name,
                    payload->xuid);
            }
        }
    }

    static void DispatchClientFullyConnected(Type& instance, const KeelLifecycleEvent& event)
    {
        if constexpr (kClientFullyConnected)
        {
            if (const auto* payload = PayloadFrom<KeelLifecycleClientFullyConnected>(event))
            {
                instance.OnClientFullyConnected(CPlayerSlot{payload->slot});
            }
        }
    }

    static void DispatchClientDisconnecting(Type& instance, const KeelLifecycleEvent& event)
    {
        if constexpr (kClientDisconnecting)
        {
            if (const auto* payload = PayloadFrom<KeelLifecycleClientDisconnecting>(event);
                payload && payload->reserved == 0)
            {
                instance.OnClientDisconnecting(
                    CPlayerSlot{payload->slot},
                    static_cast<ENetworkDisconnectionReason>(payload->reason),
                    payload->name,
                    payload->xuid,
                    payload->network_id);
            }
        }
    }

    static void DispatchClientSettingsChanged(Type& instance, const KeelLifecycleEvent& event)
    {
        if constexpr (kClientSettingsChanged)
        {
            if (const auto* payload = PayloadFrom<KeelLifecycleClientSettingsChanged>(event))
            {
                instance.OnClientSettingsChanged(CPlayerSlot{payload->slot});
            }
        }
    }
};

}

#define KEELS2_PLUGIN(type) \
    extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query( \
        const KeelHostQuery* query, \
        KeelPluginInfo* info) \
    { \
        return ::keels2::detail::AuthoringAdapter<type>::Query(query, info); \
    } \
    extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Manifest( \
        const KeelHostQuery* query, \
        KeelPluginManifest* manifest) \
    { \
        return ::keels2::detail::AuthoringAdapter<type>::Manifest(query, manifest); \
    } \
    extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load( \
        const KeelHostApi* api, \
        KeelPluginHandle plugin) \
    { \
        return ::keels2::detail::AuthoringAdapter<type>::Load(api, plugin); \
    } \
    extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin) \
    { \
        ::keels2::detail::AuthoringAdapter<type>::Unload(plugin); \
    }

#endif
