#pragma once

#include <networkbasetypes.pb.h>
#include <google/protobuf/arena.h>

#include <cstddef>
#include <new>

namespace keels2::cs2
{

class PlayerInfoMessage final
{
public:
    PlayerInfoMessage() : arena_(Options()),
        message_(google::protobuf::Arena::CreateMessage<CMsgPlayerInfo>(&arena_))
    {
    }

    CMsgPlayerInfo& Get() noexcept
    {
        return *message_;
    }

private:
    static google::protobuf::ArenaOptions Options()
    {
        google::protobuf::ArenaOptions options;
        // CS2 and this adapter use different allocators on Windows. The arena
        // retains the engine's string cleanup callbacks, while every arena
        // block returns through the adapter's allocation callbacks.
        options.block_alloc = [](std::size_t size)
        {
            return ::operator new(size);
        };

        options.block_dealloc = [](void* block, std::size_t)
        {
            ::operator delete(block);
        };
        return options;
    }

    google::protobuf::Arena arena_;
    CMsgPlayerInfo* message_;
};

}
