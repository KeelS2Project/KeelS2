#ifndef KEELS2_HOST_PUBLISHED_SERVICE_REGISTRY_H
#define KEELS2_HOST_PUBLISHED_SERVICE_REGISTRY_H

#include <keels2/services.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace keels2::host
{

class Host;

class PublishedServiceRegistry final
{
public:
    struct Snapshot
    {
        KeelServiceHandle handle{};
        KeelPluginHandle provider{};
        std::string name;
        std::uint32_t version{};
        std::size_t leases{};
    };

    explicit PublishedServiceRegistry(Host& host);
    ~PublishedServiceRegistry();
    PublishedServiceRegistry(const PublishedServiceRegistry&) = delete;
    PublishedServiceRegistry& operator=(const PublishedServiceRegistry&) = delete;

    const KeelServicesApi& Api() const noexcept;
    KeelResult Query(
        KeelPluginHandle consumer,
        const char* name,
        std::uint32_t version,
        const void** service);
    bool HasLeasedPublication(KeelPluginHandle provider, std::string& consumer) const;
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    std::vector<Snapshot> Snapshots() const;
    void Shutdown();

private:
    struct Publication
    {
        KeelServiceHandle handle{};
        KeelPluginHandle provider{};
        std::string name;
        std::uint32_t version{};
        const void* service{};
        std::unordered_set<KeelPluginHandle> consumers;
    };

    struct Key
    {
        std::string name;
        std::uint32_t version{};

        bool operator==(const Key&) const = default;
    };

    struct KeyHash
    {
        std::size_t operator()(const Key& key) const noexcept;
    };

    static KeelResult PublishEntry(
        KeelPluginHandle plugin,
        const KeelServiceSpec* spec,
        KeelServiceHandle* publication);
    static KeelResult WithdrawEntry(
        KeelPluginHandle plugin,
        KeelServiceHandle publication);
    static KeelResult ReleaseEntry(
        KeelPluginHandle plugin,
        const char* name,
        std::uint32_t version);

    KeelResult Publish(
        KeelPluginHandle plugin,
        const KeelServiceSpec* spec,
        KeelServiceHandle* publication);
    KeelResult Withdraw(KeelPluginHandle plugin, KeelServiceHandle publication);
    KeelResult Release(
        KeelPluginHandle plugin,
        const char* name,
        std::uint32_t version);
    Host& host_;
    KeelServicesApi api_{};
    KeelServiceHandle next_publication_{1};
    bool shutting_down_{};
    std::unordered_map<KeelServiceHandle, Publication> publications_;
    std::unordered_map<Key, KeelServiceHandle, KeyHash> publications_by_key_;

    static PublishedServiceRegistry* active_;
};

}

#endif
