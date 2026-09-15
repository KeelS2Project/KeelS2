#ifndef KEELS2_HOST_PLAYER_REGISTRY_H
#define KEELS2_HOST_PLAYER_REGISTRY_H

#include <keels2/players.h>

#include <cstdint>
#include <vector>

namespace keels2::host
{

class PlayerRegistry final
{
public:
    explicit PlayerRegistry(std::uint32_t capacity);

    std::uint32_t Capacity() const noexcept;
    bool ValidSlot(std::int32_t slot) const noexcept;
    KeelResult Connected(std::int32_t slot, const char* name, bool bot);
    void Disconnected(std::int32_t slot) noexcept;
    void Missing(std::int32_t slot) noexcept;
    KeelResult Update(const KeelPlayerInfo& facts, KeelPlayerInfo& player);
    bool Pending(std::int32_t slot, KeelPlayerInfo& player) const noexcept;
    bool Current(const KeelPlayerConnection& connection) const noexcept;

    static void Clear(KeelPlayerInfo& player) noexcept;

private:
    struct Entry
    {
        KeelPlayerInfo player{};
        bool disconnecting{};
        bool pending{};
    };

    std::uint64_t NextGeneration() noexcept;
    static bool ValidFacts(const KeelPlayerInfo& facts) noexcept;

    std::vector<Entry> entries_;
    std::uint64_t next_generation_{1};
};

}

#endif
