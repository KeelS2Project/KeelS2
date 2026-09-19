#ifndef KEELS2_CS2_OWNED_CONSTRUCTION_H
#define KEELS2_CS2_OWNED_CONSTRUCTION_H
#include <keels2/cs2/entity_keyvalues.h>
#include <keels2/game_adapter.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace keels2::cs2 {
// Ready enforces the game thread. Create transfers ownership only on success
// and cleans up its own failures. Validate has no game callbacks. Every native
// operation checks the saved epoch/serial before touching the current entity.
class ConstructionBackend
{
public:
    virtual ~ConstructionBackend() = default;
    virtual KeelResult Ready() = 0;
    virtual KeelResult Create(const char*, host::GameEntityIdentity&) = 0;
    virtual KeelResult Validate(const host::GameEntityIdentity&) = 0;
    virtual KeelResult Spawn(const host::GameEntityIdentity&, const void*, KeelBool&) = 0;
    virtual KeelResult Cancel(const host::GameEntityIdentity&) = 0;
    virtual KeelResult Teleport(const host::GameEntityIdentity&, const KeelEntityTeleport&) = 0;
    virtual KeelResult Visit(const host::GameEntityIdentity&, const char*, KeelEntityAccessCallback, void*) = 0;
};

class OwnedConstructions final
{
public:
    static constexpr unsigned Capacity = 64;
    explicit OwnedConstructions(ConstructionBackend& backend) : backend_(backend) {}
    ~OwnedConstructions();
    OwnedConstructions(const OwnedConstructions&) = delete;
    OwnedConstructions& operator=(const OwnedConstructions&) = delete;
    KeelResult Create(const char* class_name, std::uint64_t& token, host::GameEntityIdentity& identity) noexcept;
    KeelResult Describe(std::uint64_t token, host::GameEntityIdentity& identity) noexcept;
    KeelResult Set(std::uint64_t token, const KeelCs2EntityKeyValue& value) noexcept;
    KeelResult Teleport(std::uint64_t token, const KeelEntityTeleport& request) noexcept;
    KeelResult Spawn(std::uint64_t token, KeelBool& invoked) noexcept;
    KeelResult Cancel(std::uint64_t token) noexcept;
    KeelResult Visit(std::uint64_t token, const char* class_name, KeelEntityAccessCallback callback, void* data) noexcept;
    void Reset() noexcept;
    unsigned Count() const noexcept;
private:
    struct ReleaseValues { void operator()(void* value) const noexcept { KeelCs2KeyValues_Release(value); } };
    using Values = std::unique_ptr<void,ReleaseValues>;
    struct Key {
        KeelCs2EntityKeyValue value{};
        std::string name, text;
        KeelCs2EntityKeyValue View() const noexcept;
    };
    struct Record {
        std::uint64_t token{};
        host::GameEntityIdentity identity{};
        std::string class_name;
        std::vector<Key> keys;
        Values values;
        bool busy{}, closed{}, created{}, consumed{};
    };
    struct Operation {
        OwnedConstructions& store;
        std::shared_ptr<Record> record;
        bool was_busy{};
        Operation(OwnedConstructions& owner, std::shared_ptr<Record> value);
        ~Operation();
    };
    std::shared_ptr<Record> Find(std::uint64_t token) const noexcept;
    KeelResult Begin(std::uint64_t token, std::shared_ptr<Record>& record);
    void Detach(const std::shared_ptr<Record>& record) noexcept;
    KeelResult Finish(const std::shared_ptr<Record>& record) noexcept;
    static KeelResult CopyKey(const KeelCs2EntityKeyValue& input, Key& key);
    static KeelResult Build(const Record& record, const std::vector<Key>& keys, Values& values);
    ConstructionBackend& backend_;
    std::array<std::shared_ptr<Record>,Capacity> records_{};
    std::uint64_t next_token_{1};
    unsigned depth_{}, resetting_{};
};
}
#endif
