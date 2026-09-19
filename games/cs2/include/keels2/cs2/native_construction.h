#ifndef KEELS2_CS2_NATIVE_CONSTRUCTION_H
#define KEELS2_CS2_NATIVE_CONSTRUCTION_H
#include <keels2/cs2/owned_construction.h>
#include <keels2/cs2/native_bridge.h>

namespace keels2::cs2 {
class ConstructionEnvironment
{
public:
    virtual ~ConstructionEnvironment() = default;
    virtual KeelResult Ready() = 0;
    virtual KeelResult ResolveBase(void*&) = 0;
    // Current has no engine callbacks. An expected epoch of zero captures it.
    virtual KeelResult Current(std::uint64_t expected, void*&, std::uint64_t&) noexcept = 0;
    virtual KeelCs2EntityConstructionBindings Bindings() const noexcept = 0;
    virtual KeelResult TeleportTarget(const char*, KeelCs2EntityToolBindings&, KeelCs2EntityToolClass&) = 0;
};

class NativeConstructionBackend final : public ConstructionBackend
{
public:
    explicit NativeConstructionBackend(ConstructionEnvironment& environment) : environment_(environment) {}

    KeelResult Ready() override;
    KeelResult Create(const char*, host::GameEntityIdentity&) override;
    KeelResult Validate(const host::GameEntityIdentity&) override;
    KeelResult Spawn(const host::GameEntityIdentity&, const void*, KeelBool&) override;
    KeelResult Cancel(const host::GameEntityIdentity&) override;
    KeelResult Teleport(const host::GameEntityIdentity&, const KeelEntityTeleport&) override;
    KeelResult Visit(const host::GameEntityIdentity&, const char*, KeelEntityAccessCallback, void*) override;

private:
    KeelResult Current(const host::GameEntityIdentity&, void*&);
    ConstructionEnvironment& environment_;
};
}
#endif
