#include "game_adapter_loader.h"
#include <keels2/platform/dynamic_library.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, int line) { if (!value) throw std::runtime_error("construction adapter check "+std::to_string(line)); }
#define CHECK(value) Check((value),__LINE__)
template<class T> T Function(void* address) { T value{}; static_assert(sizeof(value) == sizeof(address)); std::memcpy(&value,&address,sizeof(value)); return value; }
}
int main(int argc, char** argv)
{
    if (argc != 4) return 1;
    try {
        namespace fs = std::filesystem;
        using namespace keels2::host;
        const auto directory = fs::temp_directory_path() / ("keels2-construction-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path,ignored); } } cleanup{directory};
        const auto adapter_path = directory/fs::path(argv[2]).filename();
        fs::copy_file(argv[2],adapter_path);
        keels2::platform::DynamicLibrary tier0, library;
        std::string error;
        CHECK(tier0.Open(argv[1],error));
        CHECK(!library.Open(adapter_path,error));
        fs::copy_file(argv[3],directory/fs::path(argv[3]).filename());
        if (!library.Open(adapter_path,error)) throw std::runtime_error(error);
        const auto query = Function<GameAdapterQueryEntityConstructionFn>(library.Symbol(kGameAdapterEntityConstructionSymbol));
        CHECK(query);
        GameAdapterEntityConstructionApi api{};
        CHECK(query(1,nullptr) == KEEL_RESULT_INVALID_ARGUMENT);
        CHECK(query(1,&api) == KEEL_RESULT_INVALID_ARGUMENT);
        api.size = sizeof(api);
        CHECK(query(99,&api) == KEEL_RESULT_INCOMPATIBLE && !api.size && !api.create);
        api.size = sizeof(api);
        CHECK(query(1,&api) == KEEL_RESULT_OK && api.size == sizeof(api) && api.api_version == 1 &&
            api.ready && api.create && api.describe && api.set && api.teleport && api.spawn && api.cancel && api.visit);
        const GameAdapterHostApi host{sizeof(host),kGameAdapterAbiVersion,[]() noexcept { return 1u; },[]() noexcept {}};
#if defined(_WIN32)
        constexpr const char* platform = "win64";
#else
        constexpr const char* platform = "linuxsteamrt64";
#endif
        GameAdapterModule module;
        if (!module.Load(directory,"cs2",platform,host,error)) throw std::runtime_error(error);
        CHECK(module.EntityConstruction().api_version == 1 && module.EntityConstruction().create == api.create);
        auto* adapter = module.Get();
        std::uint64_t token = 99;
        GameEntityIdentity identity{3,4,5};
        KeelBool invoked = KEEL_TRUE;
        CHECK(api.ready(nullptr) == KEEL_RESULT_INVALID_ARGUMENT);
        CHECK(api.create(nullptr,"prop_dynamic",&token,&identity) == KEEL_RESULT_INVALID_ARGUMENT && !token && !identity.epoch);
        CHECK(api.spawn(nullptr,1,&invoked) == KEEL_RESULT_INVALID_ARGUMENT && !invoked);
        CHECK(api.ready(adapter) == KEEL_RESULT_WRONG_THREAD);
        CHECK(api.create(adapter,"prop_dynamic",&token,&identity) == KEEL_RESULT_WRONG_THREAD && !token && !identity.epoch);
        CHECK(api.describe(adapter,1,&identity) == KEEL_RESULT_WRONG_THREAD && !identity.epoch);
        KeelEntityKeyValue key{}; key.size = sizeof(key); key.type = KEELS2_ENTITY_KEY_INT32; key.name = "value";
        KeelEntityTeleport request{sizeof(request),KEELS2_TELEPORT_POSITION,{1,2,3},{},{}};
        CHECK(api.set(adapter,1,&key) == KEEL_RESULT_WRONG_THREAD);
        CHECK(api.teleport(adapter,1,&request) == KEEL_RESULT_WRONG_THREAD);
        CHECK(api.spawn(adapter,1,&invoked) == KEEL_RESULT_WRONG_THREAD && !invoked);
        CHECK(api.cancel(adapter,1) == KEEL_RESULT_WRONG_THREAD);
        bool visited{};
        CHECK(api.visit(adapter,1,"CBaseEntity",[](void* data,void* const*,std::uint32_t) -> KeelResult {
            *static_cast<bool*>(data) = true; return KEEL_RESULT_OK;
        },&visited) == KEEL_RESULT_WRONG_THREAD && !visited);
        adapter->Stop(); module.Reset();
        CHECK(!module.EntityConstruction().size && !module.EntityConstruction().create);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
