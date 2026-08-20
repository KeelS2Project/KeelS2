#include <keels2/plugins.h>

#include <stddef.h>

_Static_assert(sizeof(KeelPluginSnapshot) == 1624, "KeelPluginSnapshot ABI changed");
_Static_assert(offsetof(KeelPluginSnapshot, handle) == 8, "snapshot handle offset changed");
_Static_assert(offsetof(KeelPluginSnapshot, name) == 16, "snapshot name offset changed");
_Static_assert(sizeof(KeelPluginEvent) == 1640, "KeelPluginEvent ABI changed");
_Static_assert(sizeof(KeelPluginSubscriptionSpec) == 32, "subscription spec ABI changed");
_Static_assert(sizeof(KeelPluginsApi) == 72, "KeelPluginsApi ABI changed");
_Static_assert(sizeof(KeelPluginDependency) == 24, "dependency ABI changed");
_Static_assert(sizeof(KeelPluginManifest) == 24, "manifest ABI changed");

int main(void)
{
    return KEELS2_PLUGINS_API_VERSION == 1u && KEELS2_PLUGIN_MANIFEST_VERSION == 1u ? 0 : 1;
}
