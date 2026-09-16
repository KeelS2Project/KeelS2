#include <networkbasetypes.pb.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <memory>

namespace
{
constexpr std::uint64_t kAllocation = UINT64_C(0x4b45454c504c4159);
struct alignas(std::max_align_t) Header { std::uint64_t marker; };
std::atomic<std::uint64_t> outstanding{};

void* Allocate(std::size_t size)
{
    if (size > SIZE_MAX - sizeof(Header)) throw std::bad_alloc();
    auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
    if (!header) throw std::bad_alloc();
    header->marker = kAllocation;
    ++outstanding;
    return header + 1;
}

void Release(void* pointer) noexcept
{
    if (!pointer) return;
    auto* header = static_cast<Header*>(pointer) - 1;
    if (header->marker != kAllocation) std::_Exit(197);
    header->marker = 0;
    --outstanding;
    std::free(header);
}

struct TextMessage
{
    google::protobuf::DescriptorPool pool;
    google::protobuf::DynamicMessageFactory factory;
    std::unique_ptr<google::protobuf::Message> message;

    explicit TextMessage(bool require_extra)
    {
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
        if (require_extra)
        {
            auto* extra = type->add_field();
            extra->set_name("required_by_engine");
            extra->set_number(3);
            extra->set_type(google::protobuf::FieldDescriptorProto::TYPE_BOOL);
            extra->set_label(google::protobuf::FieldDescriptorProto::LABEL_REQUIRED);
        }
        const auto* schema = pool.BuildFile(file);
        if (!schema) throw std::bad_alloc();
        message.reset(factory.GetPrototype(schema->message_type(0))->New());
    }
};
}

// This fixture models CS2's separate allocator. Allocations made by its copy
// of protobuf must return through this module, including long string buffers.
void* operator new(std::size_t size) { return Allocate(size); }
void* operator new[](std::size_t size) { return Allocate(size); }
void operator delete(void* pointer) noexcept { Release(pointer); }
void operator delete[](void* pointer) noexcept { Release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { Release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { Release(pointer); }

#if defined(_WIN32)
#define PLAYER_FIXTURE_EXPORT __declspec(dllexport)
#else
#define PLAYER_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" PLAYER_FIXTURE_EXPORT void KeelTest_FillPlayerInfo(
    google::protobuf::Message* message, const char* name)
{
    auto& info = *static_cast<CMsgPlayerInfo*>(message);
    info.set_name(name);
    info.set_fakeplayer(true);
    info.set_ishltv(false);
}

extern "C" PLAYER_FIXTURE_EXPORT std::uint64_t KeelTest_PlayerInfoAllocations()
{
    return outstanding.load();
}

extern "C" PLAYER_FIXTURE_EXPORT void* KeelTest_CreateTextMessage(bool require_extra)
{
    return new TextMessage(require_extra);
}

extern "C" PLAYER_FIXTURE_EXPORT google::protobuf::Message* KeelTest_TextMessage(void* owner)
{
    return static_cast<TextMessage*>(owner)->message.get();
}

extern "C" PLAYER_FIXTURE_EXPORT void KeelTest_DestroyTextMessage(void* owner)
{
    delete static_cast<TextMessage*>(owner);
}
