#ifndef KEELS2_HOST_CONVAR_SERVICE_H
#define KEELS2_HOST_CONVAR_SERVICE_H

#include "game_adapter.h"

#include <keels2/convar.h>
#include <keels2/source2_authoring.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace keels2::host
{

class Host;

class ConVarService final
{
public:
    ConVarService(Host& host, GameAdapter& adapter);
    ~ConVarService();
    ConVarService(const ConVarService&) = delete;
    ConVarService& operator=(const ConVarService&) = delete;

    const KeelConVarApi& Api() const noexcept;
    void Activate(KeelPluginHandle plugin);
    KeelResult Deactivate(KeelPluginHandle plugin);
    KeelResult ReleasePlugin(KeelPluginHandle plugin);
    bool Shutdown();
    KeelResult CreateNative(
        KeelPluginHandle plugin,
        const KeelConVarSpec* spec,
        KeelSource2ConVarChangeCallback callback,
        void* user_data,
        KeelConVarHandle* convar,
        void** native_convar);
    KeelResult FindNative(
        KeelPluginHandle plugin,
        const char* name,
        KeelConVarType expected_type,
        KeelConVarHandle* convar,
        void** native_convar);
    KeelResult ReleaseNative(KeelPluginHandle plugin, KeelConVarHandle convar);

private:
    struct Definition;
    struct Record;

    static KeelResult CreateEntry(
        KeelPluginHandle plugin,
        const KeelConVarSpec* spec,
        KeelConVarHandle* convar);
    static KeelResult FindEntry(
        KeelPluginHandle plugin,
        const char* name,
        KeelConVarType expected_type,
        KeelConVarHandle* convar);
    static KeelResult ReleaseEntry(KeelPluginHandle plugin, KeelConVarHandle convar);
    static KeelResult ReadEntry(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        std::int32_t slot,
        KeelConVarValue* value);
    static KeelResult QueueSetEntry(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        std::int32_t slot,
        const KeelConVarValue* value);
    static KeelResult DescribeEntry(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        KeelConVarInfo* info);
    static void ChangeEntry(
        std::int32_t slot,
        const KeelConVarValue& new_value,
        const KeelConVarValue& old_value,
        void* user_data);
    static void NativeChangeEntry(
        void* convar,
        std::int32_t slot,
        const void* new_value,
        const void* old_value,
        void* user_data);

    KeelResult Create(
        KeelPluginHandle plugin,
        const KeelConVarSpec* spec,
        KeelConVarHandle* convar);
    KeelResult CreateImpl(
        KeelPluginHandle plugin,
        const KeelConVarSpec* spec,
        KeelConVarChangeCallback callback,
        KeelSource2ConVarChangeCallback native_callback,
        void* user_data,
        KeelConVarHandle* convar,
        void** native_convar);
    KeelResult Find(
        KeelPluginHandle plugin,
        const char* name,
        KeelConVarType expected_type,
        KeelConVarHandle* convar);
    KeelResult FindImpl(
        KeelPluginHandle plugin,
        const char* name,
        KeelConVarType expected_type,
        KeelConVarHandle* convar,
        void** native_convar);
    KeelResult Release(KeelPluginHandle plugin, KeelConVarHandle convar);
    KeelResult Read(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        std::int32_t slot,
        KeelConVarValue* value);
    KeelResult QueueSet(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        std::int32_t slot,
        const KeelConVarValue* value);
    KeelResult Describe(
        KeelPluginHandle plugin,
        KeelConVarHandle convar,
        KeelConVarInfo* info);
    void Dispatch(
        Record& record,
        std::int32_t slot,
        const KeelConVarValue& new_value,
        const KeelConVarValue& old_value);
    void DispatchNative(
        Record& record,
        void* convar,
        std::int32_t slot,
        const void* new_value,
        const void* old_value);

    KeelResult ReleaseRecord(const std::shared_ptr<Record>& record);
    std::shared_ptr<Record> OwnedRecord(
        KeelPluginHandle plugin,
        KeelConVarHandle convar) const;
    static bool ValidType(KeelConVarType type) noexcept;
    static bool ValidLookupName(const char* name) noexcept;
    static bool ValidValue(const KeelConVarValue& value, KeelConVarType type) noexcept;
    static bool ValidDefinition(const KeelConVarSpec& spec) noexcept;
    static bool EqualDefinition(const Definition& definition, const KeelConVarSpec& spec) noexcept;
    static std::string NormalizeName(const char* name);
    static bool IsCurrentOwner(KeelPluginHandle plugin) noexcept;
    static bool IsCurrentRecord(const Record* record) noexcept;
    static void LeaveActive(std::atomic<std::uint32_t>& active) noexcept;
    static void WaitForZero(std::atomic<std::uint32_t>& active) noexcept;

    Host& host_;
    GameAdapter& adapter_;
    KeelConVarApi api_{};
    mutable std::mutex registry_mutex_;
    std::unordered_map<KeelConVarHandle, std::shared_ptr<Record>> records_;
    std::unordered_map<std::string, Definition> definitions_;
    KeelConVarHandle next_convar_{1};
    bool shutting_down_{};
    bool shutdown_complete_{};

    static std::atomic<ConVarService*> active_;
    static thread_local std::array<const Record*, 64> callback_stack_;
    static thread_local std::size_t callback_depth_;
};

}

#endif
