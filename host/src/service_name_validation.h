#ifndef KEELS2_HOST_SERVICE_NAME_VALIDATION_H
#define KEELS2_HOST_SERVICE_NAME_VALIDATION_H

#include <string>

namespace keels2::host
{

bool CanonicalServiceName(const char* name, std::string& canonical) noexcept;

}

#endif
