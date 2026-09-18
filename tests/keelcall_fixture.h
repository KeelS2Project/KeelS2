#ifndef KEELS2_TEST_KEELCALL_FIXTURE_H
#define KEELS2_TEST_KEELCALL_FIXTURE_H
#include <keels2/keelcall.h>
namespace keelcall_fixture
{
bool Check(const KeelHostApi& host, const KeelHookApi& hooks, KeelPluginHandle plugin);
}
#endif
