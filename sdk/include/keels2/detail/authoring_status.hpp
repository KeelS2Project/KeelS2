#ifndef KEELS2_DETAIL_AUTHORING_STATUS_HPP
#define KEELS2_DETAIL_AUTHORING_STATUS_HPP

#include <keels2/plugin.h>

#include <atomic>

namespace keels2::detail
{

inline const char* ResultDescription(KeelResult result) noexcept
{
    switch (result)
    {
        case KEEL_RESULT_OK: return "";
        case KEEL_RESULT_INVALID_ARGUMENT: return "invalid argument";
        case KEEL_RESULT_NOT_READY: return "service or resource is unavailable";
        case KEEL_RESULT_NOT_FOUND: return "not found";
        case KEEL_RESULT_ALREADY_EXISTS: return "already registered";
        case KEEL_RESULT_ENGINE_FAILURE: return "engine operation failed";
        case KEEL_RESULT_RESERVED_NAME: return "name is reserved";
        case KEEL_RESULT_INCOMPATIBLE: return "incompatible type, service, or game profile";
        case KEEL_RESULT_UNSUPPORTED: return "operation is unsupported";
        case KEEL_RESULT_AMBIGUOUS: return "multiple matches";
        case KEEL_RESULT_BUSY: return "resource is in use; retry after the callback";
        case KEEL_RESULT_WRONG_THREAD: return "operation requires the game thread";
        default: return "unknown service result";
    }
}

class AuthoringStatus final
{
public:
    bool Set(KeelResult result, const char* reason = nullptr) noexcept
    {
        reason_.store(reason ? reason : ResultDescription(result), std::memory_order_release);
        result_.store(result, std::memory_order_release);
        return result == KEEL_RESULT_OK;
    }

    KeelResult Result() const noexcept
    {
        return result_.load(std::memory_order_acquire);
    }

    const char* Error() const noexcept
    {
        return reason_.load(std::memory_order_acquire);
    }

private:
    std::atomic<KeelResult> result_{KEEL_RESULT_OK};
    std::atomic<const char*> reason_{""};
};

}

#endif
