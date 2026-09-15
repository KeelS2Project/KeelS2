#include <keels2/source2.hpp>
#include <keels2/factories.hpp>
#include <keels2/bootstrap_api.h>
#include <keels2/platform/dynamic_library.h>
#include <keels2/source2_runtime.hpp>

#if !defined(KEELS2_SOURCE2_LIVE)
#include "player_service_contract.h"
#endif

#include <array>
#include <charconv>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

#if !defined(KEELS2_SOURCE2_EXPECTED_PROFILE) || \
    !defined(KEELS2_SOURCE2_EXPECTED_CVAR_MODULE)
#error Source 2 gateway fixture policy is not configured
#endif

class Source2ServicePlugin final : public keels2::detail::AbiPlugin
{
public:
    keels2::PluginInfo Information() const noexcept override
    {
        return {
            "Source2 Service Test",
            "KeelS2 Project",
            "1",
            "Source 2 interface gateway integration fixture"
        };
    }

    bool Load(keels2::Context& context) override
    {
        context_ = &context;
        if (!ValidateRawService(context) ||
            service_.Connect(context) != KEEL_RESULT_OK ||
            runtime_.Connect(context) != KEEL_RESULT_OK ||
            runtime_.ServerCommand("echo loading") != KEEL_RESULT_NOT_READY ||
            !ValidateInterfaces() ||
            !ValidateNamedInterfaces())
        {
            context.Log(KEEL_LOG_ERROR, "Source 2 interface gateway load validation failed");
            context_ = nullptr;
            return false;
        }
        const KeelResult command_result = context.RegisterCommand<&Source2ServicePlugin::CheckCommand>(
            command_,
            "s2_check",
            "Validates the Source 2 interface gateway while the host is running",
            *this);
        if (command_result != KEEL_RESULT_OK)
        {
            context.Log(KEEL_LOG_ERROR, "Source 2 interface gateway command registration failed");
            context_ = nullptr;
            return false;
        }
        if (factories_.Connect(context) != KEEL_RESULT_OK ||
            factories_.Subscribe(keels2::source2::Factory::engine, "NetworkServerService_001",
                &FactoryCallback, this, 0, factory_engine_) != KEEL_RESULT_OK ||
            factories_.Subscribe(keels2::source2::Factory::server, "Source2Server001",
                &FactoryCallback, this, 0, factory_server_) != KEEL_RESULT_OK ||
            factories_.Subscribe(keels2::source2::Factory::server, "KeelS2FactoryNullProbe001",
                &FactoryCallback, this, 0, factory_null_, KEELS2_FACTORY_PROCESS_LIFETIME) != KEEL_RESULT_OK)
        {
            return false;
        }
        context.Log(KEEL_LOG_INFO, "Source 2 interface gateway load validation passed");
        return true;
    }

    void Unload(keels2::Context& context) noexcept override
    {
#if !defined(KEELS2_SOURCE2_LIVE)
        if (!players_.Unloaded(context.PluginHandle()))
        {
            context.Log(KEEL_LOG_ERROR, "player service teardown failed");
        }
#endif
        const bool invalidated = !service_ && !server_ && !game_clients_ && !cvar_ &&
            !named_engine_ && !named_server_ && !named_filesystem_ && !named_physics_ &&
            !named_network_ && !named_server_service_ && !runtime_ && !command_ && !factories_;
        context.Log(
            invalidated ? KEEL_LOG_INFO : KEEL_LOG_ERROR,
            invalidated
                ? "Source 2 interface views invalidated before unload"
                : "Source 2 interface view remained active during unload");
        context_ = nullptr;
    }

private:
    static std::uint32_t FactoryCallback(const KeelFactoryRequest* request,
        KeelFactoryResult* result, void* data)
    {
        auto& self = *static_cast<Source2ServicePlugin*>(data);
        if (std::strcmp(request->interface_name, "KeelS2FactoryNullProbe001") == 0)
        {
            self.factory_null_calls_.fetch_add(1);
            result->instance = nullptr;
            result->return_code = 37;
            return KEELS2_FACTORY_REPLACE;
        }
        if (request->factory == KEELS2_SOURCE2_FACTORY_ENGINE)
        {
            self.factory_engine_calls_.fetch_add(1);
            if (self.factory_thread_check_.exchange(false))
            {
                std::thread query([&self] {
                    const void* service{};
                    self.factory_thread_ok_.store(self.context_->QueryService(
                        KEELS2_FACTORIES_SERVICE_NAME, KEELS2_FACTORIES_API_VERSION,
                        &service) == KEEL_RESULT_OK && service);
                });
                query.join();
            }
        }
        else
        {
            self.factory_server_calls_.fetch_add(1);
        }
        return KEELS2_FACTORY_OBSERVE;
    }

    bool ValidateFactories()
    {
        factory_thread_check_.store(true);
        factory_thread_ok_.store(false);
        const auto engine_before = factory_engine_calls_.load();
        const auto server_before = factory_server_calls_.load();
        keels2::source2::Interface engine;
        if (service_.Query(keels2::source2::Factory::filesystem,
                "NetworkServerService_001", engine) != KEEL_RESULT_OK ||
            factory_engine_calls_.load() <= engine_before || !factory_thread_ok_.load())
        {
            return false;
        }
        std::string error;
        auto** table = *static_cast<void***>(server_.Raw());
        const auto factory = reinterpret_cast<KeelCreateInterfaceFn>(
            keels2::platform::ModuleSymbolFromAddress(table[0], "CreateInterface", error));
        int code = 1;
        if (!factory || factory("Source2Server001", &code) != server_.Raw() || code != 0 ||
            factory_server_calls_.load() <= server_before)
        {
            return false;
        }
        KeelFactoryResult original{};
        const auto null_before = factory_null_calls_.load();
        if (factories_.Original(keels2::source2::Factory::server,
                "KeelS2FactoryNullProbe001", original) != KEEL_RESULT_OK ||
            original.instance || original.return_code != 1 ||
            factory_null_calls_.load() != null_before ||
            factory("KeelS2FactoryNullProbe001", &code) || code != 37 ||
            factory_null_calls_.load() != null_before + 1 ||
            factories_.Unsubscribe(factory_null_) != KEEL_RESULT_OK ||
            factory("KeelS2FactoryNullProbe001", &code) || code != 1 ||
            factory_null_calls_.load() != null_before + 1)
        {
            return false;
        }
        if (factories_.Subscribe(keels2::source2::Factory::server, "KeelS2FactoryNullProbe001",
                &FactoryCallback, this, 0, factory_null_, KEELS2_FACTORY_PROCESS_LIFETIME) != KEEL_RESULT_OK)
        {
            return false;
        }
        context_->Log(KEEL_LOG_INFO, "managed factory live probes passed engine=observed server=export null=replaced original=forwarded removal=restored");
        return true;
    }

    struct Expected
    {
        keels2::source2::Capability capability;
        keels2::source2::Factory factory;
        const char* name;
        const char* module;
        keels2::source2::Interface* interface;
    };

    bool ValidateRawService(keels2::Context& context)
    {
        const void* raw = &context;
        if (context.QueryService(
                KEELS2_SOURCE2_SERVICE_NAME,
                KEELS2_SOURCE2_API_VERSION + 1,
                &raw) != KEEL_RESULT_INCOMPATIBLE || raw)
        {
            return false;
        }
        const void* runtime = &context;
        if (context.QueryService(
                KEELS2_SOURCE2_RUNTIME_SERVICE_NAME,
                KEELS2_SOURCE2_RUNTIME_API_VERSION + 1,
                &runtime) != KEEL_RESULT_INCOMPATIBLE || runtime ||
            context.QueryService(
                KEELS2_SOURCE2_RUNTIME_SERVICE_NAME,
                KEELS2_SOURCE2_RUNTIME_API_VERSION,
                &runtime) != KEEL_RESULT_OK || !runtime)
        {
            return false;
        }
        const auto* runtime_api = static_cast<const KeelSource2RuntimeApi*>(runtime);
        if (runtime_api->size != sizeof(KeelSource2RuntimeApi) ||
            runtime_api->api_version != KEELS2_SOURCE2_RUNTIME_API_VERSION ||
            !runtime_api->server_command || !runtime_api->client_console_print ||
            !runtime_api->find_user_message)
        {
            return false;
        }
        if (context.QueryService(
                KEELS2_SOURCE2_SERVICE_NAME,
                KEELS2_SOURCE2_API_VERSION,
                &raw) != KEEL_RESULT_OK || !raw)
        {
            return false;
        }
        const auto* api = static_cast<const KeelSource2Api*>(raw);
        if (api->size != sizeof(KeelSource2Api) ||
            api->api_version != KEELS2_SOURCE2_API_VERSION || !api->query_interface ||
            !api->query_named_interface)
        {
            return false;
        }

        const void* legacy_raw{};
        if (context.QueryService(
                KEELS2_SOURCE2_SERVICE_NAME,
                KEELS2_SOURCE2_API_VERSION_1,
                &legacy_raw) != KEEL_RESULT_OK || !legacy_raw)
        {
            return false;
        }
        const auto* legacy = static_cast<const KeelSource2ApiV1*>(legacy_raw);
        KeelSource2InterfaceInfo legacy_info{};
        legacy_info.size = sizeof(legacy_info);
        if (legacy->size != sizeof(KeelSource2ApiV1) ||
            legacy->api_version != KEELS2_SOURCE2_API_VERSION_1 ||
            !legacy->query_interface ||
            legacy->query_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_CAPABILITY_SERVER,
                &legacy_info) != KEEL_RESULT_OK ||
            !ValidateRawInterface(
                legacy_info,
                KEELS2_SOURCE2_CAPABILITY_SERVER,
                KEELS2_SOURCE2_FACTORY_SERVER,
                "Source2Server001"))
        {
            return false;
        }

        KeelSource2InterfaceInfo info{};
        if (api->query_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_CAPABILITY_SERVER,
                nullptr) != KEEL_RESULT_INVALID_ARGUMENT ||
            api->query_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_CAPABILITY_SERVER,
                &info) != KEEL_RESULT_INCOMPATIBLE || info.size != 0)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api->query_interface(
                context.PluginHandle(),
                0,
                &info) != KEEL_RESULT_UNSUPPORTED ||
            info.size != sizeof(info) || info.instance)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api->query_interface(
                context.PluginHandle() + 1000,
                KEELS2_SOURCE2_CAPABILITY_SERVER,
                &info) != KEEL_RESULT_NOT_READY ||
            info.size != sizeof(info) || info.instance)
        {
            return false;
        }
        info = {};
        if (api->query_named_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "NetworkServerService_001",
                nullptr) != KEEL_RESULT_INVALID_ARGUMENT ||
            api->query_named_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "NetworkServerService_001",
                &info) != KEEL_RESULT_INCOMPATIBLE || info.size != 0)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api->query_named_interface(
                context.PluginHandle(),
                0,
                "NetworkServerService_001",
                &info) != KEEL_RESULT_INVALID_ARGUMENT ||
            info.size != sizeof(info) || info.instance)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api->query_named_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "",
                &info) != KEEL_RESULT_INVALID_ARGUMENT || info.instance)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api->query_named_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "Network Server Service_001",
                &info) != KEEL_RESULT_INVALID_ARGUMENT || info.instance)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api->query_named_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "NetworkServerService_000",
                &info) != KEEL_RESULT_NOT_FOUND || info.instance)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api->query_named_interface(
                context.PluginHandle() + 1000,
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "NetworkServerService_001",
                &info) != KEEL_RESULT_NOT_READY || info.instance)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api->query_named_interface(
                context.PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "NetworkServerService_001",
                &info) != KEEL_RESULT_OK ||
            !ValidateRawInterface(
                info,
                KEELS2_SOURCE2_CAPABILITY_NAMED,
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "NetworkServerService_001"))
        {
            return false;
        }
        if (std::strncmp(
                KEELS2_SOURCE2_EXPECTED_PROFILE,
                "test-fixture-",
                sizeof("test-fixture-") - 1) == 0 &&
            !ValidateFixtureFailures(*api))
        {
            return false;
        }
        return true;
    }

    bool ValidateInterfaces()
    {
#if defined(_WIN32)
        constexpr const char* server_module = "server.dll";
#else
        constexpr const char* server_module = "libserver.so";
#endif
        const std::array expected{
            Expected{
                keels2::source2::Capability::server,
                keels2::source2::Factory::server,
                "Source2Server001",
                server_module,
                &server_},
            Expected{
                keels2::source2::Capability::game_clients,
                keels2::source2::Factory::server,
                "Source2GameClients001",
                server_module,
                &game_clients_},
            Expected{
                keels2::source2::Capability::cvar,
                keels2::source2::Factory::engine,
                "VEngineCvar007",
                KEELS2_SOURCE2_EXPECTED_CVAR_MODULE,
                &cvar_}
        };
        for (const Expected& item : expected)
        {
            if (service_.Query(item.capability, *item.interface) != KEEL_RESULT_OK ||
                !ValidateInterface(*item.interface, item))
            {
                return false;
            }
        }
        const std::array<void*, 3> instances{
            server_.Raw(),
            game_clients_.Raw(),
            cvar_.Raw()
        };
        for (std::size_t iteration = 0; iteration < 2048; ++iteration)
        {
            const Expected& item = expected[iteration % expected.size()];
            keels2::source2::Interface interface;
            if (service_.Query(item.capability, interface) != KEEL_RESULT_OK ||
                !ValidateInterface(interface, item) ||
                interface.Raw() != instances[iteration % instances.size()])
            {
                return false;
            }
        }
        return server_.Raw() != game_clients_.Raw() &&
            server_.Raw() != cvar_.Raw() && game_clients_.Raw() != cvar_.Raw();
    }

    bool ValidateNamedInterfaces()
    {
        if (service_.Query(
                keels2::source2::Factory::engine,
                "NetworkServerService_001",
                named_engine_) != KEEL_RESULT_OK ||
            !ValidateNamedInterface(
                named_engine_,
                keels2::source2::Factory::engine,
                "NetworkServerService_001") ||
            service_.Query(
                keels2::source2::Factory::server,
                "Source2Server001",
                named_server_) != KEEL_RESULT_OK ||
            !ValidateNamedInterface(
                named_server_,
                keels2::source2::Factory::server,
                "Source2Server001") ||
            named_server_.Raw() != server_.Raw() ||
            service_.Query(
                keels2::source2::Factory::filesystem,
                "VFileSystem017",
                named_filesystem_) != KEEL_RESULT_OK ||
            !ValidateNamedInterface(
                named_filesystem_,
                keels2::source2::Factory::filesystem,
                "VFileSystem017") ||
            service_.Query(
                keels2::source2::Factory::physics,
                "VPhysics2_Interface_001",
                named_physics_) != KEEL_RESULT_OK ||
            !ValidateNamedInterface(
                named_physics_,
                keels2::source2::Factory::physics,
                "VPhysics2_Interface_001") ||
            service_.Query(
                keels2::source2::Factory::network,
                "NetworkSystemVersion001",
                named_network_) != KEEL_RESULT_OK ||
            !ValidateNamedInterface(
                named_network_,
                keels2::source2::Factory::network,
                "NetworkSystemVersion001") ||
            service_.Query(
                keels2::source2::Factory::server_service,
                "NetworkServerService_001",
                named_server_service_) != KEEL_RESULT_OK ||
            !ValidateNamedInterface(
                named_server_service_,
                keels2::source2::Factory::server_service,
                "NetworkServerService_001"))
        {
            return false;
        }

        const void* const instance = named_engine_.Raw();
        const char* const name = named_engine_.Name();
        const char* const module = named_engine_.Module();
        const char* const module_path = named_engine_.ModulePath();
        const char* const profile = named_engine_.CompatibilityProfile();
        for (std::size_t iteration{}; iteration < 2048; ++iteration)
        {
            keels2::source2::Interface interface;
            if (service_.Query(
                    keels2::source2::Factory::engine,
                    "NetworkServerService_001",
                    interface) != KEEL_RESULT_OK ||
                !ValidateNamedInterface(
                    interface,
                    keels2::source2::Factory::engine,
                    "NetworkServerService_001") ||
                interface.Raw() != instance || interface.Name() != name ||
                interface.Module() != module || interface.ModulePath() != module_path ||
                interface.CompatibilityProfile() != profile)
            {
                return false;
            }
        }

        keels2::source2::Interface invalid = named_engine_;
        if (service_.Query(keels2::source2::Factory::engine, "", invalid) !=
                KEEL_RESULT_INVALID_ARGUMENT || invalid ||
            service_.Query(
                keels2::source2::Factory::engine,
                "NetworkServerService_000",
                invalid) != KEEL_RESULT_NOT_FOUND || invalid)
        {
            return false;
        }
        return true;
    }

    static bool ValidateInterface(
        const keels2::source2::Interface& interface,
        const Expected& expected)
    {
        return interface && interface.Type() == expected.capability &&
            interface.Origin() == expected.factory &&
            interface.Owner() == keels2::source2::Ownership::borrowed &&
            interface.ValidUntil() == keels2::source2::Lifetime::host &&
            interface.Name() && std::strcmp(interface.Name(), expected.name) == 0 &&
            interface.Module() && std::strcmp(interface.Module(), expected.module) == 0 &&
            interface.ModulePath() && interface.ModulePath()[0] &&
            interface.CompatibilityProfile() &&
            std::strcmp(interface.CompatibilityProfile(), KEELS2_SOURCE2_EXPECTED_PROFILE) == 0;
    }

    static bool ValidateNamedInterface(
        const keels2::source2::Interface& interface,
        keels2::source2::Factory factory,
        const char* name)
    {
        return interface && interface.Type() == keels2::source2::Capability::named &&
            interface.Origin() == factory &&
            interface.Owner() == keels2::source2::Ownership::borrowed &&
            interface.ValidUntil() == keels2::source2::Lifetime::host &&
            interface.Name() && std::strcmp(interface.Name(), name) == 0 &&
            interface.Module() && interface.Module()[0] &&
            interface.ModulePath() && interface.ModulePath()[0] &&
            interface.CompatibilityProfile() &&
            std::strcmp(interface.CompatibilityProfile(), KEELS2_SOURCE2_EXPECTED_PROFILE) == 0;
    }

    static bool ValidateRawInterface(
        const KeelSource2InterfaceInfo& info,
        KeelSource2Capability capability,
        KeelSource2Factory factory,
        const char* name)
    {
        return info.size == sizeof(info) && info.capability == capability &&
            info.factory == factory &&
            info.ownership == KEELS2_SOURCE2_OWNERSHIP_BORROWED &&
            info.lifetime == KEELS2_SOURCE2_LIFETIME_HOST && info.reserved == 0 &&
            info.instance && info.interface_name &&
            std::strcmp(info.interface_name, name) == 0 && info.module_name &&
            info.module_name[0] && info.module_path && info.module_path[0] &&
            info.compatibility_profile &&
            std::strcmp(info.compatibility_profile, KEELS2_SOURCE2_EXPECTED_PROFILE) == 0;
    }

    bool ValidateFixtureFailures(const KeelSource2Api& api)
    {
        KeelSource2InterfaceInfo info{};
        info.size = sizeof(info);
        if (api.query_named_interface(
                context_->PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "TransientService001",
                &info) != KEEL_RESULT_NOT_FOUND || info.instance)
        {
            return false;
        }
        info.size = sizeof(info);
        if (api.query_named_interface(
                context_->PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "TransientService001",
                &info) != KEEL_RESULT_OK ||
            !ValidateRawInterface(
                info,
                KEELS2_SOURCE2_CAPABILITY_NAMED,
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "TransientService001"))
        {
            return false;
        }
        info.size = sizeof(info);
        if (api.query_named_interface(
                context_->PluginHandle(),
                KEELS2_SOURCE2_FACTORY_ENGINE,
                "InconsistentService001",
                &info) != KEEL_RESULT_ENGINE_FAILURE || info.instance)
        {
            return false;
        }
        info.size = sizeof(info);
        return api.query_named_interface(
                   context_->PluginHandle(),
                   KEELS2_SOURCE2_FACTORY_ENGINE,
                   "NullVtableService001",
                   &info) == KEEL_RESULT_INCOMPATIBLE && !info.instance;
    }

    void CheckCommand(const keels2::CommandInvocation& invocation)
    {
        if (!context_)
        {
            return;
        }
#if !defined(KEELS2_SOURCE2_LIVE)
        if (invocation.Size() == 2 && std::strcmp(invocation[0], "players") == 0)
        {
            const bool passed = players_.Check(*context_, invocation[1]);
            context_->Log(passed ? KEEL_LOG_INFO : KEEL_LOG_ERROR,
                passed ? "player service contract passed {}" : "player service contract failed {}", invocation[1]);
            return;
        }
#endif
        if (invocation.Size() == 1 && std::strcmp(invocation[0], "factories") == 0)
        {
            if (!ValidateFactories())
            {
                context_->Log(KEEL_LOG_ERROR, "managed factory live probes failed");
            }
            return;
        }
        std::int32_t slot{};
        const char* slot_argument = invocation.Size() == 1 ? invocation[0] : nullptr;
        const std::string_view slot_text = slot_argument ? slot_argument : "";
        const auto parsed = std::from_chars(
            slot_text.data(),
            slot_text.data() + slot_text.size(),
            slot);
        if (parsed.ec != std::errc{} || parsed.ptr != slot_text.data() + slot_text.size() ||
            slot < 0)
        {
            context_->Log(KEEL_LOG_ERROR, "usage: s2_check <connected-client-slot>");
            return;
        }
#if defined(KEELS2_SOURCE2_LIVE)
        std::uint32_t message_id{};
        const KeelResult server_command = runtime_.ServerCommand(
            "echo [KeelS2 Live] server command passed\n");
        const KeelResult client_print = runtime_.ClientConsolePrint(
            slot,
            "[KeelS2 Live] client console print passed\n");
        const KeelResult user_message = runtime_.FindUserMessage(
            "SayText2",
            message_id);
        const bool valid = ValidateFactories() && ValidateInterfaces() && ValidateNamedInterfaces() &&
            server_command == KEEL_RESULT_OK && client_print == KEEL_RESULT_OK &&
            user_message == KEEL_RESULT_OK && message_id == 118;
        const std::string result = valid
            ? "Source 2 live runtime validation passed message_id=118"
            : "Source 2 live runtime validation failed server=" +
                std::to_string(server_command) + " client=" +
                std::to_string(client_print) + " user_message=" +
                std::to_string(user_message) + " message_id=" +
                std::to_string(message_id);
        context_->Log(valid ? KEEL_LOG_INFO : KEEL_LOG_ERROR, result.c_str());
#else
        std::uint32_t message_id{99};
        const bool valid = slot == 0 && ValidateFactories() && ValidateInterfaces() && ValidateNamedInterfaces() &&
            runtime_.ServerCommand(nullptr) == KEEL_RESULT_INVALID_ARGUMENT &&
            runtime_.ClientConsolePrint(-1, "invalid") == KEEL_RESULT_INVALID_ARGUMENT &&
            runtime_.FindUserMessage("invalid name", message_id) ==
                KEEL_RESULT_INVALID_ARGUMENT && message_id == 0 &&
            runtime_.ServerCommand("echo runtime") == KEEL_RESULT_NOT_FOUND &&
            runtime_.ClientConsolePrint(0, "runtime") == KEEL_RESULT_NOT_FOUND &&
            runtime_.FindUserMessage("missing", message_id) == KEEL_RESULT_NOT_FOUND &&
            message_id == 0 && runtime_.FindUserMessage("SayText2", message_id) ==
                KEEL_RESULT_OK && message_id == 118;
        context_->Log(
            valid ? KEEL_LOG_INFO : KEEL_LOG_ERROR,
            valid
                ? "Source 2 interface gateway runtime validation passed"
                : "Source 2 interface gateway runtime validation failed");
#endif
    }

    keels2::Context* context_{};
#if !defined(KEELS2_SOURCE2_LIVE)
    PlayerServiceContract players_;
#endif
    keels2::factories::Service factories_;
    KeelFactorySubscriptionHandle factory_engine_{};
    KeelFactorySubscriptionHandle factory_server_{};
    KeelFactorySubscriptionHandle factory_null_{};
    std::atomic<bool> factory_thread_check_{};
    std::atomic<bool> factory_thread_ok_{};
    std::atomic<std::uint32_t> factory_engine_calls_{};
    std::atomic<std::uint32_t> factory_server_calls_{};
    std::atomic<std::uint32_t> factory_null_calls_{};
    keels2::source2::Service service_;
    keels2::source2::Runtime runtime_;
    keels2::source2::Interface server_;
    keels2::source2::Interface game_clients_;
    keels2::source2::Interface cvar_;
    keels2::source2::Interface named_engine_;
    keels2::source2::Interface named_server_;
    keels2::source2::Interface named_filesystem_;
    keels2::source2::Interface named_physics_;
    keels2::source2::Interface named_network_;
    keels2::source2::Interface named_server_service_;
    keels2::Command command_;
};

KEELS2_DETAIL_EXPOSE_ABI_PLUGIN(Source2ServicePlugin)
