#include <keels2/cs2/native_bridge.h>
#include <keels2/keelhook.hpp>

#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>

#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstdio>

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
int mode{};
int allocations{}, releases{}, sends{};
std::array<uint64, (ABSOLUTE_PLAYER_LIMIT + 63) / 64> recipients{};
std::string delivered;

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
    info.Clear();
    return true;
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
    return reinterpret_cast<CNetMessage*>(&message);
}

void Release(void*, INetworkMessageInternal* type, CNetMessage* value)
{
    if (type != reinterpret_cast<INetworkMessageInternal*>(&definition) || value != reinterpret_cast<CNetMessage*>(&message))
    {
        throw std::runtime_error("wrong allocation owner");
    }
    ++releases;
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
        reflection->FieldSize(*payload, parameters) != 2 ||
        reflection->GetRepeatedString(*payload, parameters, 0) != "%s")
    {
        throw std::runtime_error("chat formatting is not literal");
    }
    delivered = reflection->GetRepeatedString(*payload, parameters, 1);
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
        google::protobuf::DescriptorPool pool;
        google::protobuf::FileDescriptorProto file;
        file.set_name("keels2_text_message_fixture.proto");
        auto* type = file.add_message_type();
        type->set_name("CUserMessageTextMsg");
        auto* destination = type->add_field();
        destination->set_name("dest");
        destination->set_number(1);
        destination->set_type(google::protobuf::FieldDescriptorProto::TYPE_UINT32);
        destination->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
        auto* parameters = type->add_field();
        parameters->set_name("param");
        parameters->set_number(2);
        parameters->set_type(google::protobuf::FieldDescriptorProto::TYPE_STRING);
        parameters->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);
        const auto* schema = pool.BuildFile(file);
        Check(schema != nullptr, "fixture schema failed");
        google::protobuf::DynamicMessageFactory factory(&pool);
        std::unique_ptr<google::protobuf::Message> proto(factory.GetPrototype(schema->message_type(0))->New());
        payload = proto.get();
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
        Check(KeelCs2_PrintChat(&engine, nullptr, &events, 3, KEEL_FALSE, text) == KEEL_RESULT_NOT_READY,
            "missing message system accepted");
        for (mode = 1; mode <= 5; ++mode)
        {
            const auto expected = mode == 1 ? KEEL_RESULT_NOT_READY :
                mode == 3 ? KEEL_RESULT_INCOMPATIBLE : KEEL_RESULT_ENGINE_FAILURE;
            Check(send(3, KEEL_FALSE, text) == expected, "native failure was not contained");
            Check(allocations == releases, "message allocation leaked after failure");
        }
        std::puts("native chat recipients, literal text, reliability, limits, ownership and failure containment passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
