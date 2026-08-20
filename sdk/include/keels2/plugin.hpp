#ifndef KEELS2_PLUGIN_HPP
#define KEELS2_PLUGIN_HPP

#include <keels2/plugin.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace keels2
{

class Plugin;

namespace kh
{

class Service;

}

namespace source2
{

class Service;

}

namespace detail
{

struct ContextState final
{
    ContextState(const KeelHostApi* value_api, KeelPluginHandle value_plugin) noexcept
        : api(value_api),
          plugin(value_plugin),
          accepting_resources(true)
    {
    }

    const KeelHostApi* api{};
    KeelPluginHandle plugin{};
    std::atomic<bool> accepting_resources{};
};

template <typename Type>
class AbiPluginAdapter;

template <typename Type>
class AuthoringAdapter;

}

class CommandInvocation final
{
public:
    explicit CommandInvocation(const KeelCommandInvocation* invocation) noexcept
        : invocation_(invocation)
    {
    }

    explicit operator bool() const noexcept
    {
        return invocation_ && invocation_->size == sizeof(KeelCommandInvocation);
    }

    const char* Name() const noexcept
    {
        return *this ? invocation_->name : nullptr;
    }

    std::size_t Size() const noexcept
    {
        return *this ? invocation_->argument_count : 0;
    }

    const char* operator[](std::size_t index) const noexcept
    {
        return *this && index < invocation_->argument_count && invocation_->arguments
            ? invocation_->arguments[index]
            : nullptr;
    }

    const KeelCommandInvocation* Raw() const noexcept
    {
        return invocation_;
    }

private:
    const KeelCommandInvocation* invocation_{};
};

class Command final
{
public:
    Command() = default;
    ~Command()
    {
        static_cast<void>(Reset());
    }

    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;

    Command(Command&& other) noexcept
    {
        MoveFrom(other);
    }

    Command& operator=(Command&& other) noexcept
    {
        if (this != &other)
        {
            static_cast<void>(Reset());
            MoveFrom(other);
        }
        return *this;
    }

    explicit operator bool() const noexcept
    {
        return handle_ && state_ &&
            state_->accepting_resources.load(std::memory_order_acquire);
    }

    KeelCommandHandle Handle() const noexcept
    {
        return *this ? handle_ : 0;
    }

    KeelResult Reset() noexcept
    {
        if (!handle_)
        {
            return KEEL_RESULT_OK;
        }
        if (!state_ ||
            !state_->accepting_resources.load(std::memory_order_acquire) || !state_->api ||
            !state_->api->unregister_command)
        {
            Clear();
            return KEEL_RESULT_NOT_READY;
        }
        const KeelResult result = state_->api->unregister_command(state_->plugin, handle_);
        if (result == KEEL_RESULT_OK || result == KEEL_RESULT_NOT_FOUND ||
            result == KEEL_RESULT_NOT_READY)
        {
            Clear();
        }
        return result;
    }

private:
    friend class Context;

    void Adopt(
        std::shared_ptr<detail::ContextState> state,
        KeelCommandHandle handle) noexcept
    {
        state_ = std::move(state);
        handle_ = handle;
    }

    void Clear() noexcept
    {
        state_.reset();
        handle_ = 0;
    }

    void MoveFrom(Command& other) noexcept
    {
        state_ = std::move(other.state_);
        handle_ = std::exchange(other.handle_, 0);
    }

    std::shared_ptr<detail::ContextState> state_;
    KeelCommandHandle handle_{};
};

class Context final
{
public:
    Context() = default;
    ~Context()
    {
        Unbind();
    }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    explicit operator bool() const noexcept
    {
        return state_ && state_->api && state_->plugin;
    }

    KeelPluginHandle PluginHandle() const noexcept
    {
        return *this ? state_->plugin : 0;
    }

    void Log(KeelLogLevel level, const char* message) const noexcept
    {
        if (*this && state_->api->log && message)
        {
            state_->api->log(state_->plugin, level, message);
        }
    }

    KeelResult RegisterCommand(const KeelCommandSpec& spec, Command& output) const noexcept
    {
        if (!AcceptingResources() || !state_->api->register_command)
        {
            return KEEL_RESULT_NOT_READY;
        }
        if (output)
        {
            return KEEL_RESULT_ALREADY_EXISTS;
        }
        KeelCommandHandle handle{};
        const KeelResult result = state_->api->register_command(
            state_->plugin,
            &spec,
            &handle);
        if (result == KEEL_RESULT_OK)
        {
            output.Adopt(state_, handle);
        }
        return result;
    }

    template <auto Method, typename Owner>
    KeelResult RegisterCommand(
        Command& output,
        const char* name,
        const char* description,
        Owner& owner,
        std::uint64_t flags = 0) const noexcept
    {
        static_assert(std::is_invocable_r_v<void, decltype(Method), Owner&, const CommandInvocation&>);
        const KeelCommandSpec spec{
            sizeof(KeelCommandSpec),
            name,
            description,
            flags,
            [](const KeelCommandInvocation* invocation, void* user_data) {
                const CommandInvocation view(invocation);
                std::invoke(Method, *static_cast<Owner*>(user_data), view);
            },
            &owner
        };
        return RegisterCommand(spec, output);
    }

    KeelResult QueryService(const char* name, std::uint32_t version, const void** service) const noexcept
    {
        if (!AcceptingResources() || !state_->api->query_service)
        {
            if (service)
            {
                *service = nullptr;
            }
            return KEEL_RESULT_NOT_READY;
        }
        return state_->api->query_service(state_->plugin, name, version, service);
    }

private:
    friend class Plugin;
    friend class kh::Service;
    friend class source2::Service;
    template <typename Type>
    friend class detail::AbiPluginAdapter;
    template <typename Type>
    friend class detail::AuthoringAdapter;

    bool AcceptingResources() const noexcept
    {
        return *this &&
            state_->accepting_resources.load(std::memory_order_acquire);
    }

    std::shared_ptr<detail::ContextState> State() const noexcept
    {
        return state_;
    }

    void Bind(const KeelHostApi* api, KeelPluginHandle plugin)
    {
        state_ = std::make_shared<detail::ContextState>(api, plugin);
    }

    void DisableResources() noexcept
    {
        if (state_)
        {
            state_->accepting_resources.store(false, std::memory_order_release);
        }
    }

    void Unbind() noexcept
    {
        if (state_)
        {
            state_->accepting_resources.store(false, std::memory_order_release);
            state_->api = nullptr;
            state_->plugin = 0;
            state_.reset();
        }
    }

    std::shared_ptr<detail::ContextState> state_;
};

struct PluginInfo
{
    const char* name;
    const char* author;
    const char* version;
    const char* description;
};

namespace detail
{

class AbiPlugin
{
public:
    virtual ~AbiPlugin() = default;
    virtual PluginInfo Information() const noexcept = 0;
    virtual bool Load(Context& context) = 0;
    virtual void Unload(Context& context) noexcept = 0;
};

template <typename Type>
class AbiPluginAdapter final
{
    static_assert(std::is_base_of_v<AbiPlugin, Type>);

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
            const PluginInfo info = Instance().Information();
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

    static KeelBool Load(const KeelHostApi* api, KeelPluginHandle plugin) noexcept
    {
        if (!api || api->size != sizeof(KeelHostApi) ||
            api->abi_version != KEELS2_PLUGIN_ABI_VERSION || !api->log ||
            !api->register_command || !api->unregister_command ||
            !api->query_service || !plugin)
        {
            return KEEL_FALSE;
        }
        try
        {
            Context& context = PluginContext();
            context.Bind(api, plugin);
            return Instance().Load(context) ? KEEL_TRUE : KEEL_FALSE;
        }
        catch (...)
        {
            return KEEL_FALSE;
        }
    }

    static void Unload(KeelPluginHandle plugin) noexcept
    {
        Context& context = PluginContext();
        if (!context || context.PluginHandle() != plugin)
        {
            return;
        }
        context.DisableResources();
        Instance().Unload(context);
        context.Unbind();
    }

private:
    static Type& Instance()
    {
        static Type instance;
        return instance;
    }

    static Context& PluginContext() noexcept
    {
        static Context context;
        return context;
    }
};

}

}

#define KEELS2_DETAIL_EXPOSE_ABI_PLUGIN(type) \
    extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Query( \
        const KeelHostQuery* query, \
        KeelPluginInfo* info) \
    { \
        return ::keels2::detail::AbiPluginAdapter<type>::Query(query, info); \
    } \
    extern "C" KEELS2_PLUGIN_EXPORT KeelBool KeelPlugin_Load( \
        const KeelHostApi* api, \
        KeelPluginHandle plugin) \
    { \
        return ::keels2::detail::AbiPluginAdapter<type>::Load(api, plugin); \
    } \
    extern "C" KEELS2_PLUGIN_EXPORT void KeelPlugin_Unload(KeelPluginHandle plugin) \
    { \
        ::keels2::detail::AbiPluginAdapter<type>::Unload(plugin); \
    }

#endif
