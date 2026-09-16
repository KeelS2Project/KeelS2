#include <keels2/cs2/native_bridge.h>
#include "native_player_info.h"

#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite.h>

#include <array>
#include <memory>
#include <cstring>

extern "C" KeelResult KeelCs2_PrintChat(void* engine_server, void* network_messages,
    void* game_events, int32_t slot, KeelBool broadcast, const char* text)
{
    if (!text || !text[0] || (broadcast != KEEL_TRUE && broadcast != KEEL_FALSE) ||
        (!broadcast && !CPlayerSlot(slot).IsValid()))
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    std::size_t length{};
    while (length <= 512 && text[length])
    {
        ++length;
    }
    if (length > 512)
    {
        return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (!engine_server || !network_messages || !game_events)
    {
        return KEEL_RESULT_NOT_READY;
    }
    try
    {
        auto* engine = static_cast<IVEngineServer2*>(engine_server);
        auto* messages = static_cast<INetworkMessages*>(network_messages);
        auto* events = static_cast<IGameEventSystem*>(game_events);
        std::array<uint64, (ABSOLUTE_PLAYER_LIMIT + 63) / 64> recipients{};
        bool found{};
        for (int candidate = 0; candidate < ABSOLUTE_PLAYER_LIMIT; ++candidate)
        {
            if (!broadcast && candidate != slot)
            {
                continue;
            }
            keels2::cs2::PlayerInfoMessage player_message;
            auto& info = player_message.Get();
            if (!engine->GetPlayerInfo(CPlayerSlot(candidate), info) || info.ishltv() ||
                engine->GetPlayerUserId(CPlayerSlot(candidate)).Get() < 0)
            {
                continue;
            }
            const auto index = static_cast<std::size_t>(candidate);
            recipients[index / 64] |= uint64{1} << (index % 64);
            found = true;
        }
        if (!found)
        {
            return broadcast ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
        }
        INetworkMessageInternal* definition = messages->FindNetworkMessagePartial("TextMsg");
        if (!definition)
        {
            return KEEL_RESULT_NOT_READY;
        }
        bool release_failed{};
        const auto release = [&](CNetMessage* message) noexcept {
            try
            {
                messages->DeallocateNetMessageAbstract(definition, message);
            }
            catch (...)
            {
                release_failed = true;
            }
        };
        std::unique_ptr<CNetMessage, decltype(release)> message(definition->AllocateMessage(), release);
        if (!message)
        {
            return KEEL_RESULT_ENGINE_FAILURE;
        }
        auto* proto = static_cast<google::protobuf::Message*>(message->AsProto());
        const auto* descriptor = proto ? proto->GetDescriptor() : nullptr;
        const auto* reflection = proto ? proto->GetReflection() : nullptr;
        const auto* destination = descriptor ? descriptor->FindFieldByName("dest") : nullptr;
        const auto* parameters = descriptor ? descriptor->FindFieldByName("param") : nullptr;
        if (!descriptor || descriptor->name() != "CUserMessageTextMsg" || !reflection ||
            !destination || destination->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_UINT32 ||
            destination->is_repeated() || !parameters ||
            parameters->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_STRING || !parameters->is_repeated())
        {
            return KEEL_RESULT_INCOMPATIBLE;
        }
        // Reflection setters would allocate adapter-owned strings inside an
        // engine-owned message. Let the engine's virtual parser populate it so
        // its destructor also owns every allocation (notably on Windows).
        using Wire = google::protobuf::internal::WireFormatLite;
        using Coded = google::protobuf::io::CodedOutputStream;
        constexpr std::size_t max_varint32_bytes = 5;
        std::array<uint8_t, 512 + 3 * max_varint32_bytes + 1> encoded{};
        auto* end = Wire::WriteUInt32ToArray(destination->number(), 3, encoded.data());
        end = Coded::WriteTagToArray(Wire::MakeTag(parameters->number(), Wire::WIRETYPE_LENGTH_DELIMITED), end);
        end = Coded::WriteVarint32ToArray(static_cast<uint32_t>(length), end);
        std::memcpy(end, text, length);
        // Check required fields without protobuf's error-string return value,
        // which would transfer another engine allocation across this boundary.
        if (!proto->ParsePartialFromArray(encoded.data(), static_cast<int>(end + length - encoded.data())) ||
            !proto->IsInitialized())
        {
            return KEEL_RESULT_ENGINE_FAILURE;
        }
        events->PostEventAbstract(CSplitScreenSlot(-1), false, ABSOLUTE_PLAYER_LIMIT,
            recipients.data(), definition, message.get(), 0, BUF_RELIABLE);
        message.reset();
        return release_failed ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}
