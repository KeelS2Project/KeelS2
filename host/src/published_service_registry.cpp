#include "published_service_registry.h"

#include "host.h"
#include "service_name_validation.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace keels2::host
{

PublishedServiceRegistry* PublishedServiceRegistry::active_{};

PublishedServiceRegistry::PublishedServiceRegistry(Host& host) : host_(host)
{
    if (active_)
    {
        throw std::runtime_error("published service registry already exists");
    }
    active_ = this;
    api_ = {
        sizeof(KeelServicesApi),
        KEELS2_SERVICES_API_VERSION,
        &PublishEntry,
        &WithdrawEntry,
        &ReleaseEntry
    };
}

PublishedServiceRegistry::~PublishedServiceRegistry()
{
    active_ = nullptr;
    Shutdown();
}

const KeelServicesApi& PublishedServiceRegistry::Api() const noexcept
{
    return api_;
}

std::size_t PublishedServiceRegistry::KeyHash::operator()(const Key& key) const noexcept
{
    const std::size_t first = std::hash<std::string>{}(key.name);
    const std::size_t second = std::hash<std::uint32_t>{}(key.version);
    return first ^ (second + 0x9e3779b9u + (first << 6u) + (first >> 2u));
}

KeelResult PublishedServiceRegistry::PublishEntry(
    KeelPluginHandle plugin,
    const KeelServiceSpec* spec,
    KeelServiceHandle* publication)
{
    PublishedServiceRegistry* registry = active_;
    if (!registry)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return registry->Publish(plugin, spec, publication);
    }
    catch (...)
    {
        registry->host_.Write(KEEL_LOG_ERROR, "exception while publishing a plugin service");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult PublishedServiceRegistry::WithdrawEntry(
    KeelPluginHandle plugin,
    KeelServiceHandle publication)
{
    PublishedServiceRegistry* registry = active_;
    if (!registry)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return registry->Withdraw(plugin, publication);
    }
    catch (...)
    {
        registry->host_.Write(KEEL_LOG_ERROR, "exception while withdrawing a plugin service");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult PublishedServiceRegistry::ReleaseEntry(
    KeelPluginHandle plugin,
    const char* name,
    std::uint32_t version)
{
    PublishedServiceRegistry* registry = active_;
    if (!registry)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        return registry->Release(plugin, name, version);
    }
    catch (...)
    {
        registry->host_.Write(KEEL_LOG_ERROR, "exception while releasing a plugin service");
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult PublishedServiceRegistry::Publish(
    KeelPluginHandle plugin,
    const KeelServiceSpec* spec,
    KeelServiceHandle* output)
{
    if (!spec || !output)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *output = 0;
    std::string name;
    if (spec->size != sizeof(KeelServiceSpec) || spec->version == 0 || !spec->service ||
        !CanonicalServiceName(spec->name, name))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(host_.state_mutex_);
    PluginRecord* owner = host_.PluginByHandle(plugin);
    if (shutting_down_ || !host_.accepting_resources_ || !owner ||
        !owner->accepting_resources || next_publication_ == 0)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const Key key{name, spec->version};
    if (publications_by_key_.contains(key))
    {
        return KEEL_RESULT_ALREADY_EXISTS;
    }
    Publication publication{
        next_publication_++,
        plugin,
        std::move(name),
        spec->version,
        spec->service,
        {}
    };
    const KeelServiceHandle handle = publication.handle;
    publications_by_key_.emplace(Key{publication.name, publication.version}, handle);
    publications_.emplace(handle, std::move(publication));
    *output = handle;
    return KEEL_RESULT_OK;
}

KeelResult PublishedServiceRegistry::Withdraw(
    KeelPluginHandle plugin,
    KeelServiceHandle publication)
{
    if (!publication)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(host_.state_mutex_);
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const auto position = publications_.find(publication);
    if (position == publications_.end() || position->second.provider != plugin)
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    if (!position->second.consumers.empty())
    {
        return KEEL_RESULT_BUSY;
    }
    publications_by_key_.erase(Key{position->second.name, position->second.version});
    publications_.erase(position);
    return KEEL_RESULT_OK;
}

KeelResult PublishedServiceRegistry::Release(
    KeelPluginHandle plugin,
    const char* requested,
    std::uint32_t version)
{
    std::string name;
    if (!version || !CanonicalServiceName(requested, name))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::scoped_lock lock(host_.state_mutex_);
    const PluginRecord* owner = host_.PluginByHandle(plugin);
    if (!owner || !owner->accepting_resources)
    {
        return KEEL_RESULT_NOT_READY;
    }
    const auto key = publications_by_key_.find(Key{name, version});
    if (key == publications_by_key_.end())
    {
        return KEEL_RESULT_NOT_FOUND;
    }
    Publication& publication = publications_.at(key->second);
    return publication.consumers.erase(plugin) != 0
        ? KEEL_RESULT_OK
        : KEEL_RESULT_NOT_FOUND;
}

KeelResult PublishedServiceRegistry::Query(
    KeelPluginHandle consumer,
    const char* requested,
    std::uint32_t version,
    const void** service)
{
    std::string name;
    if (!service || !version || !CanonicalServiceName(requested, name))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    *service = nullptr;
    const auto key = publications_by_key_.find(Key{name, version});
    if (key == publications_by_key_.end())
    {
        const bool name_exists = std::any_of(
            publications_.begin(),
            publications_.end(),
            [&](const auto& entry) { return entry.second.name == name; });
        return name_exists ? KEEL_RESULT_INCOMPATIBLE : KEEL_RESULT_NOT_FOUND;
    }
    Publication& publication = publications_.at(key->second);
    if (publication.provider == consumer)
    {
        *service = publication.service;
        return KEEL_RESULT_OK;
    }
    const PluginRecord* provider = host_.PluginByHandle(publication.provider);
    if (!provider || provider->state != PluginState::loaded || provider->transitioning)
    {
        return KEEL_RESULT_NOT_READY;
    }
    publication.consumers.insert(consumer);
    *service = publication.service;
    return KEEL_RESULT_OK;
}

bool PublishedServiceRegistry::HasLeasedPublication(
    KeelPluginHandle provider,
    std::string& consumer) const
{
    for (const auto& [handle, publication] : publications_)
    {
        static_cast<void>(handle);
        if (publication.provider != provider || publication.consumers.empty())
        {
            continue;
        }
        const KeelPluginHandle handle_consumer = *publication.consumers.begin();
        const PluginRecord* plugin = host_.PluginByHandle(handle_consumer);
        consumer = plugin ? plugin->name : std::to_string(handle_consumer);
        return true;
    }
    consumer.clear();
    return false;
}

KeelResult PublishedServiceRegistry::ReleasePlugin(KeelPluginHandle plugin)
{
    std::scoped_lock lock(host_.state_mutex_);
    for (const auto& [handle, publication] : publications_)
    {
        static_cast<void>(handle);
        if (publication.provider == plugin && !publication.consumers.empty())
        {
            return KEEL_RESULT_BUSY;
        }
    }
    for (auto& [handle, publication] : publications_)
    {
        static_cast<void>(handle);
        publication.consumers.erase(plugin);
    }
    for (auto position = publications_.begin(); position != publications_.end();)
    {
        if (position->second.provider == plugin)
        {
            publications_by_key_.erase(Key{position->second.name, position->second.version});
            position = publications_.erase(position);
        }
        else
        {
            ++position;
        }
    }
    return KEEL_RESULT_OK;
}

std::vector<PublishedServiceRegistry::Snapshot> PublishedServiceRegistry::Snapshots() const
{
    std::vector<Snapshot> output;
    output.reserve(publications_.size());
    for (const auto& [handle, publication] : publications_)
    {
        output.push_back({
            handle,
            publication.provider,
            publication.name,
            publication.version,
            publication.consumers.size()
        });
    }
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        return left.handle < right.handle;
    });
    return output;
}

void PublishedServiceRegistry::Shutdown()
{
    std::scoped_lock lock(host_.state_mutex_);
    shutting_down_ = true;
    publications_by_key_.clear();
    publications_.clear();
}

}
