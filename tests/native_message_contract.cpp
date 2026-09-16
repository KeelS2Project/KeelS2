#include <keels2/cs2/native_bridge.h>
#include <keels2/keelhook.hpp>

#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>

#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstdio>

extern "C" void KeelTest_FillPlayerInfo(google::protobuf::Message*, const char*);
extern "C" std::uint64_t KeelTest_PlayerInfoAllocations();
extern "C" void* KeelTest_CreateTextMessage(bool);
extern "C" google::protobuf::Message* KeelTest_TextMessage(void*);
extern "C" void KeelTest_DestroyTextMessage(void*);

namespace
{

struct Interface
{
    void** table;
};

std::array<void*, 256> engine_table{}, messages_table{}, definition_table{}, events_table{}, message_table{};
Interface engine{engine_table.data()}, messages{messages_table.data()}, definition{definition_table.data()};
Interface events{events_table.data()}, message{message_table.data()};
google::protobuf::Message* payload{};
void* message_owner{};
int mode{};
int allocations{}, releases{}, sends{};
std::array<uint64, (ABSOLUTE_PLAYER_LIMIT + 63) / 64> recipients{};
std::string delivered;
std::string player_name = "Allocator player";
int player_mode{};

template <auto Method, typename Function, std::size_t Size>
void Install(std::array<void*, Size>& table, Function function)
{
    const auto index = keels2::kh::VirtualIndex<Method>();
    if (!index || *index >= Size)
    {
        throw std::runtime_error("native method slot unavailable");
    }
    static_assert(sizeof(function) == sizeof(void*));
    std::memcpy(&table[*index], &function, sizeof(function));
}

bool GetPlayer(void*, CPlayerSlot slot, google::protobuf::Message& info)
{
    if (slot.Get() != 3 && slot.Get() != ABSOLUTE_PLAYER_LIMIT - 1)
    {
        return false;
    }
    KeelTest_FillPlayerInfo(&info, player_name.c_str());
    if (player_mode == 2) throw std::runtime_error("player information failure");
    return player_mode != 1;
}

class UserIdFixture final
{
public:
    virtual CPlayerUserId GetUser(CPlayerSlot slot)
    {
        return CPlayerUserId(500 + slot.Get());
    }
};

INetworkMessageInternal* FindMessage(void*, const char* name)
{
    return mode == 1 || std::strcmp(name, "TextMsg") ? nullptr : reinterpret_cast<INetworkMessageInternal*>(&definition);
}

CNetMessage* Allocate(void*)
{
    if (mode == 2)
    {
        return nullptr;
    }
    ++allocations;
    message_owner = KeelTest_CreateTextMessage(mode == 6);
    payload = KeelTest_TextMessage(message_owner);
    return reinterpret_cast<CNetMessage*>(&message);
}

void Release(void*, INetworkMessageInternal* type, CNetMessage* value)
{
    if (type != reinterpret_cast<INetworkMessageInternal*>(&definition) || value != reinterpret_cast<CNetMessage*>(&message))
    {
        throw std::runtime_error("wrong allocation owner");
    }
    ++releases;
    KeelTest_DestroyTextMessage(message_owner);
    message_owner = nullptr;
    payload = nullptr;
    if (mode == 5)
    {
        throw std::runtime_error("release fixture failure");
    }
}

void* AsProto(const void*)
{
    return mode == 3 ? nullptr : payload;
}

void Post(void*, CSplitScreenSlot slot, bool local, int count, const uint64* mask,
    INetworkMessageInternal* type, const CNetMessage* value, unsigned long size, NetChannelBufType_t buffer)
{
    if (slot.Get() != -1 || local || count != ABSOLUTE_PLAYER_LIMIT || !mask || size || buffer != BUF_RELIABLE ||
        type != reinterpret_cast<INetworkMessageInternal*>(&definition) || value != reinterpret_cast<CNetMessage*>(&message))
    {
        throw std::runtime_error("native message transport changed");
    }
    ++sends;
    if (mode == 4)
    {
        throw std::runtime_error("send fixture failure");
    }
    std::memcpy(recipients.data(), mask, sizeof(recipients));
    const auto* fields = payload->GetDescriptor();
    const auto* reflection = payload->GetReflection();
    const auto* parameters = fields->FindFieldByName("param");
    if (reflection->GetUInt32(*payload, fields->FindFieldByName("dest")) != 3 ||
        reflection->FieldSize(*payload, parameters) != 1)
    {
        throw std::runtime_error("chat formatting is not literal");
    }
    delivered = reflection->GetRepeatedString(*payload, parameters, 0);
}

void Check(bool condition, const char* text)
{
    if (!condition)
    {
        throw std::runtime_error(text);
    }
}

}

int main()
{
    try
    {
        // The engine owns both the message and its protobuf implementation.
        // Its allocator rejects memory created by the adapter's implementation.
        auto* initial_message = KeelTest_CreateTextMessage(false);
        KeelTest_DestroyTextMessage(initial_message);
        Install<&IVEngineServer2::GetPlayerInfo>(engine_table, &GetPlayer);
        UserIdFixture user_id;
        void** user_id_table{};
        std::memcpy(&user_id_table, &user_id, sizeof(user_id_table));
        Install<&IVEngineServer2::GetPlayerUserId>(engine_table, user_id_table[0]);
        Install<&INetworkMessages::FindNetworkMessagePartial>(messages_table, &FindMessage);
        Install<&INetworkMessages::DeallocateNetMessageAbstract>(messages_table, &Release);
        Install<&INetworkMessageInternal::AllocateMessage>(definition_table, &Allocate);
        Install<&CNetMessage::AsProto>(message_table, &AsProto);
        using PostMethod = void (IGameEventSystem::*)(CSplitScreenSlot, bool, int, const uint64*,
            INetworkMessageInternal*, const CNetMessage*, unsigned long, NetChannelBufType_t);
        Install<static_cast<PostMethod>(&IGameEventSystem::PostEventAbstract)>(events_table, &Post);
        const auto read_player = [&] {
            KeelPlayerInfo player{};
            const auto result = KeelCs2_ReadPlayer(&engine, nullptr, nullptr, nullptr, 3, &player);
            const auto expected = player_mode == 1 ? KEEL_RESULT_NOT_FOUND :
                player_mode == 2 ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK;
            Check(result == expected, "player information result changed");
            if (!player_mode) {
                const auto expected_name = player_name.substr(0, sizeof(player.name) - 1);
                Check(player.slot == 3 && player.user_id == 503 &&
                    (player.flags & KEELS2_PLAYER_BOT) && player.name == expected_name,
                    "player information was not copied before cleanup");
            } else Check(player.slot == -1 && !player.name[0], "failed player lookup retained output");
        };
        read_player();
        const auto player_allocations = KeelTest_PlayerInfoAllocations();
        for (const auto& name : {std::string{}, std::string("Bot"), std::string(512, 'N')}) {
            player_name = name;
            for (player_mode = 0; player_mode != 3; ++player_mode) {
                for (int iteration = 0; iteration != 20; ++iteration) read_player();
                if (KeelTest_PlayerInfoAllocations() != player_allocations) {
                    std::fprintf(stderr, "name length %zu, mode %d, allocations %llu -> %llu\n",
                        name.size(), player_mode, static_cast<unsigned long long>(player_allocations),
                        static_cast<unsigned long long>(KeelTest_PlayerInfoAllocations()));
                }
                Check(KeelTest_PlayerInfoAllocations() == player_allocations,
                    "player information allocation leaked across the engine boundary");
            }
        }
        player_mode = 0;
        const char* text = "100% %s %n {literal}\n; quit";
        auto send = [&](int slot, KeelBool broadcast, const char* value) {
            return KeelCs2_PrintChat(&engine, &messages, &events, slot, broadcast, value);
        };
        Check(send(3, KEEL_FALSE, text) == KEEL_RESULT_OK && delivered == text && recipients[0] == 8 &&
            allocations == 1 && releases == 1 && sends == 1, "single-recipient literal delivery failed");
        Check(send(-1, KEEL_TRUE, text) == KEEL_RESULT_OK && delivered == text, "broadcast failed");
        const auto last = static_cast<std::size_t>(ABSOLUTE_PLAYER_LIMIT - 1);
        Check((recipients[last / 64] & (uint64{1} << (last % 64))) != 0 && (recipients[0] & 8), "recipient mask truncated");
        Check(send(ABSOLUTE_PLAYER_LIMIT, KEEL_FALSE, text) == KEEL_RESULT_INVALID_ARGUMENT &&
            send(-1, KEEL_FALSE, text) == KEEL_RESULT_INVALID_ARGUMENT &&
            send(3, 2, text) == KEEL_RESULT_INVALID_ARGUMENT &&
            send(3, KEEL_FALSE, nullptr) == KEEL_RESULT_INVALID_ARGUMENT &&
            send(2, KEEL_FALSE, text) == KEEL_RESULT_NOT_FOUND, "invalid recipients accepted");
        const std::string limit(512, 'x');
        Check(send(3, KEEL_FALSE, limit.c_str()) == KEEL_RESULT_OK &&
            send(3, KEEL_FALSE, (limit + "x").c_str()) == KEEL_RESULT_INVALID_ARGUMENT, "message limit changed");
        for (const std::size_t size : {1u, 15u, 16u, 127u, 128u, 512u})
        {
            const std::string value(size, '%');
            Check(send(3, KEEL_FALSE, value.c_str()) == KEEL_RESULT_OK && delivered == value,
                "chat string size or allocation ownership changed");
            Check(KeelTest_PlayerInfoAllocations() == player_allocations,
                "engine-owned chat message leaked an allocation");
        }
        Check(KeelCs2_PrintChat(&engine, nullptr, &events, 3, KEEL_FALSE, text) == KEEL_RESULT_NOT_READY,
            "missing message system accepted");
        for (mode = 1; mode <= 6; ++mode)
        {
            const auto expected = mode == 1 ? KEEL_RESULT_NOT_READY :
                mode == 3 ? KEEL_RESULT_INCOMPATIBLE : KEEL_RESULT_ENGINE_FAILURE;
            Check(send(3, KEEL_FALSE, text) == expected, "native failure was not contained");
            Check(allocations == releases, "message allocation leaked after failure");
        }
        Check(KeelTest_PlayerInfoAllocations() == player_allocations,
            "chat recipient information leaked across the engine boundary");
        std::puts("native chat recipients, literal text, reliability, limits, ownership and failure containment passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
