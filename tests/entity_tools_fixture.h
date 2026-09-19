// Included inside the native schema fixture's anonymous namespace.
unsigned g_tool_calls{}, g_tool_flags{};
bool g_tool_destroy{}, g_tool_throw{}, g_tool_arguments{};
std::array<float,9> g_tool_vectors{};
std::string g_tool_asset;

void EntityToolTeleport(void* entity, const Vector* position, const QAngle* angles, const Vector* velocity)
{
    ++g_tool_calls;
    g_tool_arguments = entity == EntityInstance();
    g_tool_flags = (position ? 1u : 0u) | (angles ? 2u : 0u) | (velocity ? 4u : 0u);

    if (position)
    {
        g_tool_vectors[0] = position->x;
        g_tool_vectors[1] = position->y;
        g_tool_vectors[2] = position->z;
    }

    if (angles)
    {
        g_tool_vectors[3] = angles->x;
        g_tool_vectors[4] = angles->y;
        g_tool_vectors[5] = angles->z;
    }

    if (velocity)
    {
        g_tool_vectors[6] = velocity->x;
        g_tool_vectors[7] = velocity->y;
        g_tool_vectors[8] = velocity->z;
    }

    if (g_tool_destroy)
    {
        Identity()->m_pInstance = nullptr;
        StorePointer(g_entity_storage.data(), nullptr);
    }

    if (g_tool_throw)
        throw std::runtime_error("engine fixture failure");
}

void* EntityToolSetModel(void* entity, const char* asset)
{
    ++g_tool_calls;
    g_tool_arguments = entity == EntityInstance();
    g_tool_asset = asset;

    if (g_tool_destroy)
    {
        Identity()->m_pInstance = nullptr;
        StorePointer(g_entity_storage.data(), nullptr);
    }

    return entity;
}

void EntityToolRemove(void* entity)
{
    ++g_tool_calls;
    g_tool_arguments = entity == EntityInstance();
    Identity()->m_pInstance = nullptr;
    StorePointer(g_entity_storage.data(), nullptr);
}

int RunEntityToolChecks()
{
    Reset();
#if defined(_WIN32)
    constexpr const char* module = "server.dll";
    constexpr unsigned slot = 163;
#else
    constexpr const char* module = "libserver.so";
    constexpr unsigned slot = 162;
#endif
    std::array<void*,slot+1> table{}, other{};
    table[slot] = FunctionAddress(&EntityToolTeleport);
    StorePointer(g_entity_storage.data(), table.data());
    const KeelCs2EntityToolBindings bindings{
        FunctionAddress(&EntityToolSetModel), FunctionAddress(&EntityToolRemove), slot, 0};

    const KeelCs2EntityToolClass target{table.data(), table[slot]};
    KeelCs2EntityIdentity entity{};

    if (KeelCs2_FindEntityByIndex(EntitySystem(), kEntityIndex, &entity) != KEEL_RESULT_OK)
        return 1001;

    KeelCs2EntityToolContext context{};
    void* base{};
    const auto prepare = [&](unsigned kind) {
        const auto result = KeelCs2_ResolveEntityToolBase(&g_schema_system,module,kind,&base);
        return result == KEEL_RESULT_OK ? KeelCs2_PrepareEntityTool(EntitySystem(),&entity,base,&context) : result;
    };
    KeelEntityTeleport request{sizeof(request), 7, {1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    const auto apply = [&](unsigned kind, const KeelEntityTeleport* teleport, const char* asset = nullptr) {
        return KeelCs2_ApplyEntityTool(EntitySystem(),&entity,&context,&bindings,&target,kind,teleport,asset);
    };
    g_tool_calls = 0;
    g_tool_destroy = g_tool_throw = false;

    if (prepare(1) != KEEL_RESULT_OK || context.class_info != &g_derived_class ||
        std::strcmp(context.class_name, "CCSPlayerPawn") || apply(1, &request) != KEEL_RESULT_OK || !g_tool_arguments ||
        g_tool_flags != 7 || g_tool_vectors != std::array<float, 9>{1, 2, 3, 4, 5, 6, 7, 8, 9})
        return 1002;

    for (unsigned flags = 1; flags <= 7; ++flags) {
        request.flags = flags;

        if (apply(1, &request) != KEEL_RESULT_OK || g_tool_flags != flags)
            return 1003;
    }

    const auto calls = g_tool_calls;

    for (unsigned flags : {0u, 8u, UINT32_MAX})
    {
        request.flags = flags;

        if (apply(1, &request) != KEEL_RESULT_INVALID_ARGUMENT)
            return 1004;
    }

    request.flags = 7;
    --request.size;

    if (apply(1, &request) != KEEL_RESULT_INVALID_ARGUMENT)
        return 1005;

    ++request.size;
    request.position[0] = std::numeric_limits<float>::infinity();

    if (apply(1, &request) != KEEL_RESULT_INVALID_ARGUMENT || g_tool_calls != calls)
        return 1006;

    request.flags = 4;

    if (apply(1, &request) != KEEL_RESULT_OK || g_tool_flags != 4)
        return 1007;

    request.position[0] = 1;
    auto wrong = target;
    wrong.teleport = bindings.remove;

    if (KeelCs2_ApplyEntityTool(EntitySystem(), &entity, &context, &bindings, &wrong, 1, &request, nullptr) !=
        KEEL_RESULT_INCOMPATIBLE)
        return 1008;

    StorePointer(g_entity_storage.data(),other.data());

    if (apply(1, &request) != KEEL_RESULT_INCOMPATIBLE)
        return 1009;

    StorePointer(g_entity_storage.data(),table.data());
    SetHandle(*Identity(), 13);

    if (apply(1, &request) != KEEL_RESULT_NOT_FOUND)
        return 1010;

    SetHandle(*Identity(), 12);
    Identity()->m_flags = EF_IS_PRE_SPAWN;

    if (apply(1, &request) != KEEL_RESULT_NOT_FOUND)
        return 1011;

    Identity()->m_flags = static_cast<EntityFlags_t>(0);
    const auto original = context;
    context.class_info = &g_base_class;

    if (apply(1, &request) != KEEL_RESULT_INCOMPATIBLE)
        return 1012;

    context = original;

    if (apply(2, nullptr, "models/test.vmdl") != KEEL_RESULT_INCOMPATIBLE ||
        apply(1, &request, "bad") != KEEL_RESULT_INVALID_ARGUMENT ||
        apply(8, nullptr) != KEEL_RESULT_INVALID_ARGUMENT || apply(4, &request) != KEEL_RESULT_INVALID_ARGUMENT)
        return 1013;

    g_tool_model = g_base_class;
    g_tool_model.m_pszName = "CBaseModelEntity";
    SchemaBaseClassInfoData_t model_base{0,&g_base_class};
    g_tool_model.m_nBaseClassCount = 1;
    g_tool_model.m_pBaseClasses = &model_base;
    g_derived_bases[0].m_pClass = &g_tool_model;

    if (prepare(2) != KEEL_RESULT_OK || apply(2,nullptr,"models/test.vmdl") != KEEL_RESULT_OK ||
        !g_tool_arguments || g_tool_asset != "models/test.vmdl") return 1014;

    std::string asset(511,'a');

    if (apply(2, nullptr, asset.c_str()) != KEEL_RESULT_OK || g_tool_asset != asset)
        return 1015;

    asset.push_back('a');

    for (const char* invalid : {"","models/\ntest.vmdl",asset.c_str()})
        if (apply(2, nullptr, invalid) != KEEL_RESULT_INVALID_ARGUMENT)
            return 1016;

    model_base.m_nOffset = 8;

    if (prepare(1) != KEEL_RESULT_INCOMPATIBLE)
        return 1017;

    model_base.m_nOffset = 0;

    if (prepare(1) != KEEL_RESULT_OK)
        return 1018;

    g_tool_throw = true;

    if (apply(1, &request) != KEEL_RESULT_ENGINE_FAILURE)
        return 1019;

    g_tool_throw = false;
    g_tool_destroy = true;

    if (apply(1, &request) != KEEL_RESULT_OK || Identity()->m_pInstance)
        return 1020;

    Identity()->m_pInstance = EntityInstance();
    StorePointer(g_entity_storage.data(), table.data());

    if (prepare(2) != KEEL_RESULT_OK || apply(2, nullptr, "models/test.vmdl") != KEEL_RESULT_OK ||
        Identity()->m_pInstance)
        return 1021;

    Identity()->m_pInstance = EntityInstance();
    StorePointer(g_entity_storage.data(), table.data());

    if (prepare(4) != KEEL_RESULT_OK || apply(4,nullptr) != KEEL_RESULT_OK || Identity()->m_pInstance ||
        apply(4,nullptr) != KEEL_RESULT_NOT_FOUND) return 1022;

    if (KeelCs2_PrepareEntityTool(EntitySystem(), &entity, base, &context) != KEEL_RESULT_NOT_FOUND ||
        context.class_info || context.class_name[0])
        return 1023;

    base = &g_base_class;

    if (KeelCs2_ResolveEntityToolBase(&g_schema_system, module, 8, &base) != KEEL_RESULT_INVALID_ARGUMENT || base)
        return 1024;

    Reset();
    return 0;
}
