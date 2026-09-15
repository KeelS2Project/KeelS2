#include "service_name_validation.h"

#include <keels2/services.h>

#include <cctype>

namespace keels2::host
{

bool CanonicalServiceName(const char* name, std::string& canonical) noexcept
{
    canonical.clear();
    if (!name)
    {
        return false;
    }
    try
    {
        std::size_t length{};
        while (length < KEELS2_SERVICE_NAME_CAPACITY && name[length])
        {
            const unsigned char character = static_cast<unsigned char>(name[length]);
            if (!(std::isalnum(character) || character == '.' || character == '_' ||
                    character == '-'))
            {
                canonical.clear();
                return false;
            }
            canonical.push_back(static_cast<char>(std::tolower(character)));
            ++length;
        }
        if (length == 0 || length >= KEELS2_SERVICE_NAME_CAPACITY ||
            canonical.rfind("keels2.", 0) == 0)
        {
            canonical.clear();
            return false;
        }
    }
    catch (...)
    {
        canonical.clear();
        return false;
    }
    return true;
}

}
