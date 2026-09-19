// Included inside the native schema fixture's anonymous namespace.
unsigned g_construction_mode{}, g_create_calls{}, g_spawn_calls{}, g_cancel_calls{};
bool g_construction_arguments{};
char* g_construction_source{};
std::string g_construction_name;
const void* g_construction_values{};

void* CreateEntityFixture(const char* name, int forced_index)
{
    ++g_create_calls;
    g_construction_arguments = forced_index == -1;

    if (g_construction_source)
        g_construction_source[0] = 'X';

    g_construction_name = name;

    if (g_construction_mode == 1)
        return nullptr;

    if (g_construction_mode == 2)
        return reinterpret_cast<void*>(1);

    if (g_construction_mode == 3)
        throw std::runtime_error("factory failure");

    Identity()->m_flags = g_construction_mode == 7 ? static_cast<EntityFlags_t>(0) : EF_IS_PRE_SPAWN;
    return EntityInstance();
}

void SpawnEntityFixture(void* entity, const void* key_values)
{
    ++g_spawn_calls;
    g_construction_arguments = entity == Identity()->m_pInstance && key_values == g_construction_values;
    Identity()->m_flags = static_cast<EntityFlags_t>(0);

    if (g_construction_mode == 4 || g_construction_mode == 5) {
        Identity()->m_pInstance = nullptr;

        if (entity != EntityInstance())
            ::operator delete(entity, std::align_val_t{16});
        else
            g_entity_storage.fill(std::byte{0xdd});
    }

    if (g_construction_mode == 5)
        throw std::runtime_error("spawn callback failure");

    if (g_construction_mode == 6)
        SetHandle(*Identity(), 13);
}

void CancelEntityFixture(void* entity)
{
    ++g_cancel_calls;
    g_construction_arguments = entity == Identity()->m_pInstance;
    Identity()->m_pInstance = nullptr;

    if (entity != EntityInstance())
        ::operator delete(entity, std::align_val_t{16});
    else
        g_entity_storage.fill(std::byte{0xdd});

    if (g_construction_mode == 5)
        throw std::runtime_error("remove callback failure");
}

int RunEntityConstructionChecks()
{
    Reset();
    const KeelCs2EntityConstructionBindings bindings{FunctionAddress(&CreateEntityFixture),
        FunctionAddress(&SpawnEntityFixture),FunctionAddress(&CancelEntityFixture)};

    void* instance{};
    const auto create = [&](const char* name = "prop_dynamic") {
        instance = reinterpret_cast<void*>(1);
        return KeelCs2_CreateEntity(&bindings,name,&instance);
    };
    KeelCs2EntityIdentity entity{};
    const auto capture = [&]
    {
        return KeelCs2_CaptureCreatedEntity(EntitySystem(), instance, &entity);
    };
    const auto validate = [&](KeelBool pending = KEEL_TRUE) {
        return KeelCs2_ValidateCreatedEntity(EntitySystem(),&entity,&g_base_class,pending);
    };
    KeelBool invoked = KEEL_FALSE;
    const auto spawn = [&] {
        invoked = KEEL_TRUE;
        return KeelCs2_SpawnCreatedEntity(EntitySystem(),&entity,&g_base_class,&bindings,g_construction_values,&invoked);
    };
    const auto cancel = [&] {
        invoked = KEEL_TRUE;
        return KeelCs2_RemoveCreatedEntity(EntitySystem(),&entity,&bindings,&invoked);
    };
    g_construction_mode = g_create_calls = g_spawn_calls = g_cancel_calls = 0;
    char name[] = "prop_dynamic";
    g_construction_source = name;

    if (create(name) != KEEL_RESULT_OK || !g_construction_arguments || g_construction_name != "prop_dynamic" ||
        name[0] != 'X' || capture() != KEEL_RESULT_OK || entity.index != kEntityIndex || validate() != KEEL_RESULT_OK ||
        KeelCs2_ValidateEntity(EntitySystem(), &entity) != KEEL_RESULT_NOT_FOUND)
        return 1101;

    g_construction_source = nullptr;
    std::string longest(127,'a');

    if (create(longest.c_str()) != KEEL_RESULT_OK || g_construction_name != longest)
        return 1102;

    longest.push_back('a');
    const auto creates = g_create_calls;

    for (const char* invalid : {"","bad class","bad;class","bad/class","bad\nclass",longest.c_str()})
        if (create(invalid) != KEEL_RESULT_INVALID_ARGUMENT || instance)
            return 1103;

    if (g_create_calls != creates ||
        KeelCs2_CreateEntity(&bindings, nullptr, &instance) != KEEL_RESULT_INVALID_ARGUMENT || instance ||
        KeelCs2_CreateEntity(&bindings, "prop_dynamic", nullptr) != KEEL_RESULT_INVALID_ARGUMENT)
        return 1104;

    auto incomplete = bindings;
    incomplete.remove = nullptr;

    if (KeelCs2_CreateEntity(&incomplete, "prop_dynamic", &instance) != KEEL_RESULT_INVALID_ARGUMENT || instance)
        return 1105;

    for (unsigned mode : {1u,2u,3u}) {
        g_construction_mode = mode;
        const auto result = create();

        if (result != (mode == 1 ? KEEL_RESULT_NOT_FOUND : mode == 3 ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK))
            return 1106;

        if (mode == 2) {
            entity = {123,456};

            if (capture() != KEEL_RESULT_NOT_FOUND || entity.index || entity.source2_handle)
                return 1107;
        } else if (instance) return 1108;
    }

    g_construction_mode = 0;

    if (create() != KEEL_RESULT_OK || capture() != KEEL_RESULT_OK)
        return 1109;

    const auto saved = entity;

    for (auto flags : {EF_IS_INVALID_EHANDLE,EF_DELETE_IN_PROGRESS,EF_MARKED_FOR_DELETE,EF_IS_CONSTRUCTION_IN_PROGRESS}) {
        Identity()->m_flags = static_cast<EntityFlags_t>(EF_IS_PRE_SPAWN | flags);

        if (validate() != KEEL_RESULT_NOT_FOUND || spawn() != KEEL_RESULT_NOT_FOUND || invoked ||
            cancel() != KEEL_RESULT_NOT_FOUND || invoked) return 1110;
    }

    Identity()->m_flags = EF_IS_PRE_SPAWN;
    SetHandle(*Identity(),13);

    if (validate() != KEEL_RESULT_NOT_FOUND || spawn() != KEEL_RESULT_NOT_FOUND || invoked)
        return 1111;

    SetHandle(*Identity(),12);

    for (int index : {-1,MAX_TOTAL_ENTITIES,kEntityIndex+1}) {
        entity.index = index;

        if (validate() != KEEL_RESULT_NOT_FOUND)
            return 1112;
    }

    entity = saved;
    entity.source2_handle = INVALID_EHANDLE_INDEX;

    if (validate() != KEEL_RESULT_NOT_FOUND)
        return 1113;

    entity = saved;
    EntityInstance()->m_pEntity = nullptr;

    if (validate() != KEEL_RESULT_NOT_FOUND || capture() != KEEL_RESULT_NOT_FOUND)
        return 1114;

    EntityInstance()->m_pEntity = Identity();
    entity = saved;
    Identity()->m_pInstance = reinterpret_cast<CEntityInstance*>(g_entity_storage.data()+1);

    if (validate() != KEEL_RESULT_NOT_FOUND)
        return 1115;

    Identity()->m_pInstance = EntityInstance();
    g_derived_bases[0].m_nOffset = 8;

    if (validate() != KEEL_RESULT_INCOMPATIBLE || spawn() != KEEL_RESULT_INCOMPATIBLE || invoked)
        return 1116;

    g_derived_bases[0].m_nOffset = 0;
    g_base_class.m_pszName = "WrongBase";

    if (validate() != KEEL_RESULT_INCOMPATIBLE)
        return 1117;

    g_base_class.m_pszName = "CBaseEntity";

    if (validate(static_cast<KeelBool>(7)) != KEEL_RESULT_INVALID_ARGUMENT || g_spawn_calls || g_cancel_calls)
        return 1118;

    if (spawn() != KEEL_RESULT_OK || !invoked || !g_construction_arguments || validate() != KEEL_RESULT_NOT_FOUND ||
        validate(KEEL_FALSE) != KEEL_RESULT_OK || KeelCs2_ValidateEntity(EntitySystem(),&entity) != KEEL_RESULT_OK) return 1119;

    if (spawn() != KEEL_RESULT_NOT_FOUND || invoked || g_spawn_calls != 1)
        return 1120;

    if (cancel() != KEEL_RESULT_OK || !invoked || !g_construction_arguments || cancel() != KEEL_RESULT_NOT_FOUND ||
        invoked || g_cancel_calls != 1)
        return 1121;

    for (unsigned mode : {4u,5u,6u}) {
        Reset();
        g_construction_mode = mode;

        if (create() != KEEL_RESULT_OK || capture() != KEEL_RESULT_OK)
            return 1122;

        if (spawn() != (mode == 5 ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK) || !invoked || !g_construction_arguments ||
            validate(KEEL_FALSE) != KEEL_RESULT_NOT_FOUND || cancel() != KEEL_RESULT_NOT_FOUND || invoked) return 1123;
    }

    for (unsigned mode : {0u,5u}) {
        Reset();
        g_construction_mode = mode;

        if (create() != KEEL_RESULT_OK || capture() != KEEL_RESULT_OK ||
            cancel() != (mode == 5 ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK) || !invoked || !g_construction_arguments ||
            cancel() != KEEL_RESULT_NOT_FOUND || invoked) return 1124;
    }

    Reset();
    g_construction_mode = 0;

    if (create() != KEEL_RESULT_OK || capture() != KEEL_RESULT_OK)
        return 1125;

    auto* chunk = EntitySystem()->m_EntityList.m_pIdentityChunks[0];
    EntitySystem()->m_EntityList.m_pIdentityChunks[0] = nullptr;

    if (validate() != KEEL_RESULT_NOT_FOUND || capture() != KEEL_RESULT_NOT_FOUND || entity.source2_handle)
        return 1126;

    EntitySystem()->m_EntityList.m_pIdentityChunks[0] = chunk;

    if (KeelCs2_CaptureCreatedEntity(nullptr, instance, &entity) != KEEL_RESULT_INVALID_ARGUMENT ||
        entity.source2_handle ||
        KeelCs2_SpawnCreatedEntity(EntitySystem(), &saved, &g_base_class, &bindings, nullptr, nullptr) !=
            KEEL_RESULT_INVALID_ARGUMENT)
        return 1127;

    for (unsigned mode : {0u,7u}) {
        Reset();
        g_construction_mode = mode;

        if (create() != KEEL_RESULT_OK || capture() != KEEL_RESULT_OK)
            return 1128;

        g_base_class.m_pszName = "UnsupportedBase";

        if (spawn() != (mode == 7 ? KEEL_RESULT_NOT_FOUND : KEEL_RESULT_INCOMPATIBLE) || invoked ||
            cancel() != KEEL_RESULT_OK || !invoked) return 1129;
    }

    for (unsigned mode : {4u,5u}) {
        for (bool spawning : {false,true}) {
            Reset();
            g_construction_mode = mode;

            if (create() != KEEL_RESULT_OK)
                return 1130;

            auto* allocated = static_cast<std::byte*>(::operator new(kEntitySize, std::align_val_t{16}));
            std::memcpy(allocated,g_entity_storage.data(),kEntitySize);
            Identity()->m_pInstance = reinterpret_cast<CEntityInstance*>(allocated);
            instance = allocated;

            if (capture() != KEEL_RESULT_OK)
            {
                ::operator delete(allocated, std::align_val_t{16});
                return 1131;
            }

            if ((spawning ? spawn() : cancel()) != (mode == 5 ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK) || !invoked ||
                !g_construction_arguments || validate(KEEL_FALSE) != KEEL_RESULT_NOT_FOUND ||
                capture() != KEEL_RESULT_NOT_FOUND || entity.source2_handle) return 1132;
        }
    }

    Reset();
    g_construction_mode = 0;
    std::array<unsigned, 4> key_values{1, 2, 3, 4};
    g_construction_values = key_values.data();

    if (create() != KEEL_RESULT_OK || capture() != KEEL_RESULT_OK || spawn() != KEEL_RESULT_OK ||
        !invoked || !g_construction_arguments) return 1133;

    g_construction_values = nullptr;
    Reset();

    if (create() != KEEL_RESULT_OK || capture() != KEEL_RESULT_OK)
        return 1134;

    void* pointer = reinterpret_cast<void*>(1);

    if (KeelCs2_ResolveEntityPointer(EntitySystem(), &entity, "CCSPlayerPawn", &pointer) != KEEL_RESULT_NOT_FOUND ||
        pointer ||
        KeelCs2_ResolveCreatedEntityPointer(EntitySystem(), &entity, "CCSPlayerPawn", &pointer) != KEEL_RESULT_OK ||
        pointer != instance ||
        KeelCs2_ResolveCreatedEntityPointer(EntitySystem(), &entity, "CBaseEntity", &pointer) !=
            KEEL_RESULT_INCOMPATIBLE ||
        pointer)
        return 1135;

    const auto entity_size = g_derived_class.m_nSize;
    g_derived_class.m_nSize = 8;

    if (KeelCs2_ResolveCreatedEntityPointer(EntitySystem(), &entity, "CCSPlayerPawn", &pointer) !=
            KEEL_RESULT_INCOMPATIBLE ||
        pointer)
        return 1141;

    g_derived_class.m_nSize = entity_size;
#if defined(_WIN32)
    constexpr unsigned teleport_slot = 163;
#else
    constexpr unsigned teleport_slot = 162;
#endif
    std::array<void*,teleport_slot+1> table{};
    table[teleport_slot] = FunctionAddress(&EntityToolTeleport);
    StorePointer(g_entity_storage.data(), table.data());
    const KeelCs2EntityToolBindings tools{
        FunctionAddress(&EntityToolSetModel), FunctionAddress(&EntityToolRemove), teleport_slot, 0};

    const KeelCs2EntityToolClass target{table.data(), table[teleport_slot]};
    KeelCs2EntityToolContext context{};

    if (KeelCs2_PrepareEntityTool(EntitySystem(),&entity,&g_base_class,&context) != KEEL_RESULT_NOT_FOUND ||
        KeelCs2_PrepareCreatedEntityTool(EntitySystem(),&entity,&g_base_class,&context) != KEEL_RESULT_OK) return 1136;

    KeelEntityTeleport request{sizeof(request), 7, {1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    const auto teleport = [&]
    {
        return KeelCs2_TeleportCreatedEntity(EntitySystem(), &entity, &context, &tools, &target, &request);
    };
    g_tool_calls = 0;
    g_tool_destroy = g_tool_throw = false;

    if (teleport() != KEEL_RESULT_OK || g_tool_calls != 1 || !g_tool_arguments || g_tool_flags != 7 ||
        g_tool_vectors != std::array<float, 9>{1, 2, 3, 4, 5, 6, 7, 8, 9})
        return 1137;

    Identity()->m_flags = static_cast<EntityFlags_t>(0);

    if (teleport() != KEEL_RESULT_NOT_FOUND ||
        KeelCs2_ResolveCreatedEntityPointer(EntitySystem(), &entity, "CCSPlayerPawn", &pointer) !=
            KEEL_RESULT_NOT_FOUND ||
        pointer)
        return 1138;

    Identity()->m_flags = EF_IS_PRE_SPAWN;
    SetHandle(*Identity(),13);

    if (teleport() != KEEL_RESULT_NOT_FOUND)
        return 1139;

    SetHandle(*Identity(), 12);
    g_tool_destroy = g_tool_throw = true;

    if (teleport() != KEEL_RESULT_ENGINE_FAILURE || g_tool_calls != 2 || Identity()->m_pInstance ||
        teleport() != KEEL_RESULT_NOT_FOUND) return 1140;

    g_tool_destroy = g_tool_throw = false;
    Reset();
    return 0;
}
