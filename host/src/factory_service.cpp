#include "factory_service.h"

#include "host.h"
#include "keelhook_service.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace keels2::host
{

std::atomic<FactoryService*> FactoryService::active_{};
thread_local std::array<FactoryService::DispatchScope, 64> FactoryService::stack_{};
thread_local std::size_t FactoryService::depth_{};
thread_local std::uint32_t FactoryService::bypass_{};

FactoryService::FactoryService(Host& host, KeelHookService& hooks,
    KeelCreateInterfaceFn engine, KeelCreateInterfaceFn server)
    : host_(host), hooks_(hooks)
{
    const std::array functions{engine, server};
    for (std::size_t index{}; index < functions.size(); ++index)
    {
        if (index && functions[index] == functions[0])
        {
            continue;
        }
        auto boundary = std::make_unique<Boundary>();
        boundary->service = this;
        boundary->factory = index == 0
            ? KEELS2_SOURCE2_FACTORY_ENGINE : KEELS2_SOURCE2_FACTORY_SERVER;
        boundary->function = functions[index];
        boundaries_.push_back(std::move(boundary));
    }
    api_ = {sizeof(KeelFactoriesApi), KEELS2_FACTORIES_API_VERSION,
        &SubscribeEntry, &UnsubscribeEntry, &OriginalEntry};
    active_.store(this, std::memory_order_release);
}

bool FactoryService::Initialize()
{
    for (const auto& boundary : boundaries_)
    {
        KeelHookTargetSpec target{};
        target.size = sizeof(target);
        target.source = KH_TARGET_ADDRESS;
        target.mechanism = KH_MECHANISM_DETOUR;
        static_assert(sizeof(target.address) == sizeof(boundary->function));
        std::memcpy(&target.address, &boundary->function, sizeof(target.address));
        const KeelHookValueType arguments[]{KH_VALUE_POINTER, KH_VALUE_POINTER};
        KeelHookPrototype prototype{};
        prototype.size = sizeof(prototype);
        prototype.calling_convention = KH_CALL_NATIVE;
        prototype.return_type = KH_VALUE_POINTER;
        prototype.argument_count = 2;
        prototype.fixed_argument_count = 2;
        prototype.argument_types = arguments;
        if (hooks_.Api().resolve_target(0, &target, &prototype, &boundary->target) != KEEL_RESULT_OK)
        {
            return false;
        }
        const KeelHookCallbackSpec callback{
            sizeof(KeelHookCallbackSpec), KH_PHASE_PRE, 0, 0, &DispatchEntry, boundary.get()};
        if (hooks_.Api().add_callback(0, boundary->target, &callback,
                &boundary->callback) != KEEL_RESULT_OK)
        {
            return false;
        }
    }
    return true;
}

FactoryService::~FactoryService()
{
    active_.store(nullptr, std::memory_order_release);
}

const KeelFactoriesApi& FactoryService::Api() const noexcept
{
    return api_;
}

FactoryService::Boundary* FactoryService::FindBoundary(KeelSource2Factory factory) noexcept
{
    if (factory < KEELS2_SOURCE2_FACTORY_ENGINE || factory > KEELS2_SOURCE2_FACTORY_SERVER_SERVICE)
    {
        return nullptr;
    }
    return boundaries_[factory == KEELS2_SOURCE2_FACTORY_SERVER && boundaries_.size() > 1 ? 1 : 0].get();
}

bool FactoryService::ValidName(const char* name) noexcept
{
    if (!name || !name[0])
    {
        return false;
    }
    for (std::size_t index{}; index < 256; ++index)
    {
        const unsigned char value = static_cast<unsigned char>(name[index]);
        if (!value)
        {
            return true;
        }
        if (!std::isalnum(value) && value != '_')
        {
            return false;
        }
    }
    return false;
}

void FactoryService::Wait(std::atomic<std::uint32_t>& count) noexcept
{
    for (auto value = count.load(std::memory_order_acquire); value;
        value = count.load(std::memory_order_acquire))
    {
        count.wait(value, std::memory_order_acquire);
    }
}

void FactoryService::Leave(std::atomic<std::uint32_t>& count) noexcept
{
    if (count.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        count.notify_all();
    }
}

void FactoryService::Activate(KeelPluginHandle plugin)
{
    std::scoped_lock lock(mutex_);
    for (const auto& [handle, subscription] : subscriptions_)
    {
        static_cast<void>(handle);
        if (subscription->owner == plugin && !stopping_)
        {
            subscription->enabled.store(true, std::memory_order_release);
        }
    }
}

bool FactoryService::Pinned(KeelPluginHandle plugin) const
{
    std::scoped_lock lock(mutex_);
    return pinned_.contains(plugin);
}

KeelResult FactoryService::Deactivate(KeelPluginHandle plugin)
{
    std::vector<std::shared_ptr<Subscription>> owned;
    {
        std::scoped_lock lock(mutex_);
        if (depth_ || queries_.load(std::memory_order_acquire) || pinned_.contains(plugin))
        {
            return KEEL_RESULT_BUSY;
        }
        for (const auto& [handle, subscription] : subscriptions_)
        {
            static_cast<void>(handle);
            if (subscription->owner == plugin)
            {
                subscription->enabled.store(false, std::memory_order_release);
                owned.push_back(subscription);
            }
        }
    }
    for (const auto& subscription : owned)
    {
        Wait(subscription->active);
    }
    return Pinned(plugin) ? KEEL_RESULT_BUSY : KEEL_RESULT_OK;
}

KeelResult FactoryService::ReleasePlugin(KeelPluginHandle plugin)
{
    const KeelResult result = Deactivate(plugin);
    if (result != KEEL_RESULT_OK)
    {
        return result;
    }
    std::scoped_lock lock(mutex_);
    std::erase_if(subscriptions_, [plugin](const auto& entry) {
        return entry.second->owner == plugin;
    });
    return KEEL_RESULT_OK;
}

bool FactoryService::Shutdown()
{
    std::vector<std::shared_ptr<Subscription>> subscriptions;
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
        for (const auto& [handle, subscription] : subscriptions_)
        {
            static_cast<void>(handle);
            subscription->enabled.store(false, std::memory_order_release);
            subscriptions.push_back(subscription);
        }
    }
    if (depth_)
    {
        return false;
    }
    Wait(queries_);
    for (const auto& subscription : subscriptions)
    {
        Wait(subscription->active);
    }
    std::scoped_lock lock(mutex_);
    return pinned_.empty();
}

std::vector<std::string> FactoryService::Diagnostics() const
{
    std::scoped_lock lock(mutex_);
    std::vector<std::string> result;
    result.push_back("factory boundaries=" + std::to_string(boundaries_.size()) +
        " subscriptions=" + std::to_string(subscriptions_.size()) +
        " process-pinned providers=" + std::to_string(pinned_.size()));
    for (const auto& [handle, subscription] : subscriptions_)
    {
        result.push_back("factory subscription=" + std::to_string(handle) +
            " provider=" + std::to_string(subscription->owner) +
            " scope=" + std::to_string(subscription->factory) +
            " name=" + subscription->name + " priority=" + std::to_string(subscription->priority) +
            " enabled=" + (subscription->enabled.load() ? "yes" : "no") +
            " active=" + std::to_string(subscription->active.load()));
    }
    for (const auto provider : pinned_)
    {
        result.push_back("factory replacement provider=" + std::to_string(provider) +
            " retained until process exit; pause/unload/reload/host release blocked");
    }
    std::sort(result.begin() + 1, result.end());
    return result;
}

KeelResult FactoryService::SubscribeEntry(KeelPluginHandle plugin,
    const KeelFactorySubscriptionSpec* spec, KeelFactorySubscriptionHandle* output)
{
    if (output)
    {
        *output = 0;
    }
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->Subscribe(plugin, spec, output) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult FactoryService::UnsubscribeEntry(KeelPluginHandle plugin,
    KeelFactorySubscriptionHandle handle)
{
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->Unsubscribe(plugin, handle) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult FactoryService::OriginalEntry(KeelPluginHandle plugin,
    KeelSource2Factory factory, const char* name, KeelFactoryResult* result)
{
    if (!result)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (result->size != sizeof(*result))
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    result->instance = nullptr;
    result->return_code = 1;
    try
    {
        auto* service = active_.load(std::memory_order_acquire);
        return service ? service->Query(plugin, factory, name, *result, true) : KEEL_RESULT_NOT_READY;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult FactoryService::Subscribe(KeelPluginHandle plugin,
    const KeelFactorySubscriptionSpec* spec, KeelFactorySubscriptionHandle* output)
{
    if (!spec || !output)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (spec->size != sizeof(*spec))
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    auto* boundary = FindBoundary(spec->factory);
    if (!boundary || !ValidName(spec->interface_name) || !spec->callback ||
        (spec->flags & ~KEELS2_FACTORY_PROCESS_LIFETIME))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock host_lock(host_.state_mutex_);
    std::scoped_lock lock(mutex_);
    const auto* owner = host_.PluginByHandle(plugin);
    if (stopping_ || !host_.accepting_resources_ || !owner || !owner->accepting_resources ||
        owner->transitioning)
    {
        return KEEL_RESULT_NOT_READY;
    }
    if (!next_)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    auto subscription = std::make_shared<Subscription>();
    subscription->handle = next_++;
    subscription->owner = plugin;
    subscription->factory = boundary->factory;
    subscription->name = spec->interface_name;
    subscription->priority = spec->priority;
    subscription->flags = spec->flags;
    subscription->callback = spec->callback;
    subscription->user_data = spec->user_data;
    subscription->enabled.store(owner->state == PluginState::loaded && !owner->loading &&
        owner->factory_dispatch_enabled);
    subscriptions_.emplace(subscription->handle, subscription);
    *output = subscription->handle;
    return KEEL_RESULT_OK;
}

KeelResult FactoryService::Unsubscribe(KeelPluginHandle plugin,
    KeelFactorySubscriptionHandle handle)
{
    std::shared_ptr<Subscription> subscription;
    {
        std::scoped_lock lock(mutex_);
        const auto found = subscriptions_.find(handle);
        if (found == subscriptions_.end() || found->second->owner != plugin)
        {
            return KEEL_RESULT_NOT_FOUND;
        }
        subscription = found->second;
        subscription->enabled.store(false, std::memory_order_release);
        if (depth_ && subscription->active.load(std::memory_order_acquire))
        {
            return KEEL_RESULT_BUSY;
        }
    }
    Wait(subscription->active);
    std::scoped_lock lock(mutex_);
    subscriptions_.erase(handle);
    return KEEL_RESULT_OK;
}

KeelResult FactoryService::Query(KeelPluginHandle plugin, KeelSource2Factory factory,
    const char* name, KeelFactoryResult& result, bool original)
{
    auto* boundary = FindBoundary(factory);
    if (!boundary || !ValidName(name))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    {
        std::scoped_lock host_lock(host_.state_mutex_);
        std::scoped_lock lock(mutex_);
        const auto* owner = host_.PluginByHandle(plugin);
        if (stopping_ || !host_.accepting_resources_ || !owner || !owner->accepting_resources)
        {
            return KEEL_RESULT_NOT_READY;
        }
        queries_.fetch_add(1, std::memory_order_acq_rel);
    }
    struct Scope
    {
        FactoryService& service;
        bool original;
        ~Scope()
        {
            if (original)
            {
                --bypass_;
            }
            Leave(service.queries_);
        }
    } scope{*this, original};
    if (original)
    {
        ++bypass_;
    }
    int code = 1;
    result.instance = boundary->function(name, &code);
    result.return_code = code;
    return KEEL_RESULT_OK;
}

KeelResult FactoryService::QueryNamed(KeelPluginHandle plugin, KeelSource2Factory factory,
    const char* name, KeelSource2InterfaceInfo* info)
{
    if (!info)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (info->size != sizeof(*info))
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    {
        std::scoped_lock host_lock(host_.state_mutex_);
        std::scoped_lock lock(mutex_);
        const auto* owner = host_.PluginByHandle(plugin);
        if (stopping_ || !host_.accepting_resources_ || !owner || !owner->accepting_resources)
        {
            return KEEL_RESULT_NOT_READY;
        }
        queries_.fetch_add(1, std::memory_order_acq_rel);
    }
    struct QueryScope
    {
        std::atomic<std::uint32_t>& count;
        ~QueryScope() { Leave(count); }
    } query_scope{queries_};
    *info = {};
    info->size = sizeof(*info);
    KeelFactoryResult result{sizeof(result), 1, nullptr};
    const auto status = Query(plugin, factory, name, result, false);
    if (status != KEEL_RESULT_OK)
    {
        return status;
    }
    if (!result.instance)
    {
        return result.return_code == 0 ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_NOT_FOUND;
    }
    if (result.return_code != 0)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
    auto** table = *static_cast<void***>(result.instance);
    std::filesystem::path path;
    std::string error;
    if (!table || !table[0] || !platform::ModulePathFromAddress(table[0], path, error) ||
        path.filename().empty())
    {
        return KEEL_RESULT_INCOMPATIBLE;
    }
    std::scoped_lock host_lock(host_.state_mutex_);
    std::scoped_lock lock(mutex_);
    if (stopping_)
    {
        return KEEL_RESULT_NOT_READY;
    }
    auto found = std::find_if(descriptions_.begin(), descriptions_.end(), [&](const auto& entry) {
        return entry.name == name && entry.path == path.string();
    });
    if (found == descriptions_.end())
    {
        descriptions_.push_back({name, path.filename().string(), path.string()});
        found = std::prev(descriptions_.end());
    }
    *info = {sizeof(*info), KEELS2_SOURCE2_CAPABILITY_NAMED, factory,
        KEELS2_SOURCE2_OWNERSHIP_BORROWED, KEELS2_SOURCE2_LIFETIME_HOST, 0,
        result.instance, found->name.c_str(), found->module.c_str(), found->path.c_str(),
        host_.compatibility_profile_.c_str()};
    return KEEL_RESULT_OK;
}

KeelHookAction FactoryService::DispatchEntry(KeelHookFrame* frame, void* data)
{
    auto& boundary = *static_cast<Boundary*>(data);
    return boundary.service->Dispatch(boundary, *frame);
}

KeelHookAction FactoryService::Dispatch(Boundary& boundary, KeelHookFrame& frame)
{
    const auto* name = static_cast<const char*>(frame.arguments[0].scalar.pointer);
    if (bypass_ || !ValidName(name) || depth_ == stack_.size())
    {
        return KH_ACTION_CONTINUE;
    }
    for (std::size_t index{}; index < depth_; ++index)
    {
        if (stack_[index].boundary == &boundary && std::strcmp(stack_[index].name, name) == 0)
        {
            return KH_ACTION_CONTINUE;
        }
    }
    std::vector<std::shared_ptr<Subscription>> callbacks;
    {
        std::scoped_lock lock(mutex_);
        if (stopping_)
        {
            return KH_ACTION_CONTINUE;
        }
        for (const auto& [handle, subscription] : subscriptions_)
        {
            static_cast<void>(handle);
            if (subscription->factory == boundary.factory && subscription->name == name &&
                subscription->enabled.load(std::memory_order_acquire))
            {
                callbacks.push_back(subscription);
            }
        }
    }
    if (callbacks.empty())
    {
        return KH_ACTION_CONTINUE;
    }
    std::sort(callbacks.begin(), callbacks.end(), [](const auto& left, const auto& right) {
        return left->priority != right->priority
            ? left->priority > right->priority : left->handle < right->handle;
    });
    stack_[depth_++] = {&boundary, name};
    struct Scope
    {
        ~Scope() { stack_[--depth_] = {}; }
    } scope;
    auto* caller_code = static_cast<int*>(frame.arguments[1].scalar.pointer);
    int code = 1;
    frame.arguments[1].scalar.pointer = &code;
    const auto original_status = hooks_.Api().call_original(0, &frame);
    frame.arguments[1].scalar.pointer = caller_code;
    if (original_status != KEEL_RESULT_OK)
    {
        return KH_ACTION_CONTINUE;
    }
    KeelFactoryRequest request{sizeof(request), boundary.factory, name,
        frame.result.scalar.pointer, frame.result.scalar.pointer, code, code};
    bool replaced{};
    for (const auto& callback : callbacks)
    {
        callback->active.fetch_add(1, std::memory_order_acq_rel);
        struct ActiveScope
        {
            std::atomic<std::uint32_t>& count;
            ~ActiveScope() { Leave(count); }
        } active{callback->active};
        if (!callback->enabled.load(std::memory_order_acquire))
        {
            continue;
        }
        KeelFactoryResult replacement{sizeof(replacement), request.current_return_code,
            request.current_result};
        try
        {
            const auto action = callback->callback(&request, &replacement, callback->user_data);
            if (!replaced && action == KEELS2_FACTORY_REPLACE &&
                replacement.size == sizeof(replacement) &&
                (callback->flags & KEELS2_FACTORY_PROCESS_LIFETIME))
            {
                if (replacement.instance)
                {
                    std::scoped_lock lock(mutex_);
                    pinned_.insert(callback->owner);
                }
                request.current_result = replacement.instance;
                request.current_return_code = replacement.return_code;
                replaced = true;
            }
        }
        catch (...)
        {
            host_.Write(KEEL_LOG_ERROR, "plugin threw during a factory callback");
        }
    }
    if (caller_code)
    {
        *caller_code = request.current_return_code;
    }
    frame.result.scalar.pointer = request.current_result;
    return KH_ACTION_SUPERSEDE;
}

}
