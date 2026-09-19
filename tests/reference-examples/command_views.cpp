#include <keels2/plugin.hpp>
#include <cstring>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<keels2::Command>);
static_assert(!std::is_copy_assignable_v<keels2::Command>);

int main()
{
    const char* arguments[]{"one", "two words"};
    KeelCommandInvocation raw{
        sizeof(KeelCommandInvocation), 2, "keel_docs_portable", arguments
    };
    const keels2::CommandInvocation invocation(&raw);

    if (!invocation || invocation.Raw() != &raw || invocation.Size() != 2 ||
        std::strcmp(invocation.Name(), "keel_docs_portable") != 0 ||
        std::strcmp(invocation[0], "one") != 0 ||
        std::strcmp(invocation[1], "two words") != 0 || invocation[2] != nullptr) return 1;

    raw.arguments = nullptr;

    if (!invocation || invocation.Size() != 2 || invocation[0] != nullptr)
        return 2;

    raw.size = 0;

    if (invocation || invocation.Name() || invocation.Size() || invocation[0] ||
        invocation.Raw() != &raw) return 3;

    const keels2::CommandInvocation missing(nullptr);

    if (missing || missing.Raw() || missing.Name() || missing.Size() || missing[0])
        return 4;

    keels2::Command empty;
    keels2::Command moved(std::move(empty));
    empty = std::move(moved);

    if (empty || moved || empty.Handle() || empty.Reset() != KEEL_RESULT_OK)
        return 5;

    return 0;
}
