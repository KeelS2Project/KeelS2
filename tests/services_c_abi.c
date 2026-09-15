#include <keels2/services.h>

#include <stddef.h>

_Static_assert(sizeof(KeelServiceHandle) == 8, "KeelServiceHandle layout changed");
_Static_assert(offsetof(KeelServiceSpec, size) == 0, "KeelServiceSpec size offset changed");
_Static_assert(offsetof(KeelServiceSpec, version) == 4, "KeelServiceSpec version offset changed");
_Static_assert(offsetof(KeelServiceSpec, name) == 8, "KeelServiceSpec name offset changed");
_Static_assert(offsetof(KeelServiceSpec, service) == 16, "KeelServiceSpec service offset changed");
_Static_assert(sizeof(KeelServiceSpec) == 24, "KeelServiceSpec layout changed");
_Static_assert(offsetof(KeelServicesApi, publish) == 8, "KeelServicesApi publish offset changed");
_Static_assert(sizeof(KeelServicesApi) == 32, "KeelServicesApi layout changed");

int main(void)
{
    KeelServiceSpec spec = {
        sizeof(KeelServiceSpec),
        1u,
        "example.service",
        &spec
    };
    KeelServicesApi api = {
        sizeof(KeelServicesApi),
        KEELS2_SERVICES_API_VERSION,
        NULL,
        NULL,
        NULL
    };
    return spec.size == sizeof(KeelServiceSpec) &&
            api.api_version == KEELS2_SERVICES_API_VERSION
        ? 0
        : 1;
}
