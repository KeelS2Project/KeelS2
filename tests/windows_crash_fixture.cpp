#define NOMINMAX
#include <windows.h>

int main()
{
    constexpr DWORD kContractException = 0xe0424b53u;
    RaiseException(kContractException, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return 1;
}
