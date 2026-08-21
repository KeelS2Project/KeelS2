#include <keels2/source2.hpp>

#include <array>
#include <cstring>

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
            service_.Connect(context) != KEEL_RESULT_OK || !ValidateInterfaces() ||
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
        context.Log(KEEL_LOG_INFO, "Source 2 interface gateway load validation passed");
        return true;
    }

    void Unload(keels2::Context& context) noexcept override
    {
        const bool invalidated = !service_ && !server_ && !game_clients_ && !cvar_ &&
            !named_engine_ && !named_server_ && !command_;
        context.Log(
            invalidated ? KEEL_LOG_INFO : KEEL_LOG_ERROR,
            invalidated
                ? "Source 2 interface views invalidated before unload"
                : "Source 2 interface view remained active during unload");
        context_ = nullptr;
    }

private:
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
            named_server_.Raw() != server_.Raw())
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

    void CheckCommand(const keels2::CommandInvocation&)
    {
        if (!context_)
        {
            return;
        }
        const bool valid = ValidateInterfaces() && ValidateNamedInterfaces();
        context_->Log(
            valid ? KEEL_LOG_INFO : KEEL_LOG_ERROR,
            valid
                ? "Source 2 interface gateway runtime validation passed"
                : "Source 2 interface gateway runtime validation failed");
    }

    keels2::Context* context_{};
    keels2::source2::Service service_;
    keels2::source2::Interface server_;
    keels2::source2::Interface game_clients_;
    keels2::source2::Interface cvar_;
    keels2::source2::Interface named_engine_;
    keels2::source2::Interface named_server_;
    keels2::Command command_;
};

KEELS2_DETAIL_EXPOSE_ABI_PLUGIN(Source2ServicePlugin)
