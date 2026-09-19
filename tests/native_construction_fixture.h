// Included inside the native schema fixture's anonymous namespace.
std::function<void()> g_native_create_callback, g_native_spawn_callback;
unsigned g_native_mode{}, g_native_spawns{}, g_native_removes{};
bool g_native_values_valid{};
void* g_native_retained{};

void* NativeFactory(const char* name, int index)
{
    if (std::strcmp(name, "prop_dynamic") || index != -1)
        throw std::runtime_error("factory arguments");

    if (g_native_mode == 8) {
        const auto callback = g_native_create_callback;

        if (callback)
            callback();

        return EntityInstance();
    }

    if (g_native_mode != 1)
        SetHandle(*Identity(), 13);

    Identity()->m_pInstance = EntityInstance();
    EntityInstance()->m_pEntity = Identity();
    Identity()->m_flags = g_native_mode == 6 ? static_cast<EntityFlags_t>(0) : EF_IS_PRE_SPAWN;

    if (g_native_mode == 2)
        g_base_class.m_pszName = "Unsupported";

    if (g_native_create_callback)
        g_native_create_callback();

    return EntityInstance();
}

void NativeSpawn(void*, const void* values)
{
    ++g_native_spawns;
    g_native_values_valid = values && KeelFixtureKeyValuesInspect(const_cast<void*>(values),"prop_dynamic",nullptr,0) == 0;

    if (g_native_spawn_callback)
        g_native_spawn_callback();

    if (g_native_mode == 7) {
        g_native_retained = const_cast<void*>(values);
        KeelFixtureKeyValuesRetain(g_native_retained);
        return;
    }

    if (g_native_mode == 3)
        return;

    if (g_native_mode == 4)
        throw std::runtime_error("blocked spawn");

    Identity()->m_flags = g_native_mode == 5 ? static_cast<EntityFlags_t>(EF_IS_PRE_SPAWN | EF_SPAWN_IN_PROGRESS)
                                             : static_cast<EntityFlags_t>(0);
}

void NativeRemove(void*)
{
    ++g_native_removes;
    Identity()->m_pInstance = nullptr;
}

class NativeEnvironment final : public keels2::cs2::ConstructionEnvironment
{
public:
    std::uint64_t epoch{1};
    KeelResult ready{KEEL_RESULT_OK};
    void* system{EntitySystem()};
    std::function<void()> on_base, on_target;
#if defined(_WIN32)
    static constexpr unsigned teleport_slot = 163;
#else
    static constexpr unsigned teleport_slot = 162;
#endif
    std::array<void*,teleport_slot+1> table{};

    NativeEnvironment()
    {
        table[teleport_slot] = FunctionAddress(&EntityToolTeleport);
    }

    KeelResult Ready() override
    {
        return ready;
    }

    KeelResult ResolveBase(void*& base) override {
        if (on_base)
            on_base();

        base = &g_base_class;
        return ready;
    }

    KeelResult Current(std::uint64_t expected, void*& output, std::uint64_t& captured) noexcept override {
        output = nullptr;
        captured = 0;

        if (ready != KEEL_RESULT_OK)
            return ready;

        if (expected && expected != epoch)
            return KEEL_RESULT_NOT_FOUND;

        output = system;
        captured = epoch;
        return KEEL_RESULT_OK;
    }

    KeelCs2EntityConstructionBindings Bindings() const noexcept override {
        return {FunctionAddress(&NativeFactory), FunctionAddress(&NativeSpawn), FunctionAddress(&NativeRemove)};
    }

    KeelResult
    TeleportTarget(const char* name, KeelCs2EntityToolBindings& bindings, KeelCs2EntityToolClass& target) override
    {
        if (on_target)
            on_target();

        if (std::strcmp(name, "CCSPlayerPawn"))
            return KEEL_RESULT_INCOMPATIBLE;

        bindings = {FunctionAddress(&EntityToolSetModel), FunctionAddress(&NativeRemove), teleport_slot, 0};
        target = {table.data(), table[teleport_slot]};
        return KEEL_RESULT_OK;
    }
};

int RunNativeConstructionChecks()
{
    using IdentityType = keels2::host::GameEntityIdentity;
    using Store = keels2::cs2::OwnedConstructions;
    KeelFixtureKeyValuesMemoryStart();

    struct MemoryScope
    {
        ~MemoryScope()
        {
            KeelFixtureKeyValuesMemoryStop();
        }
    } memory;
    NativeEnvironment environment;
    keels2::cs2::NativeConstructionBackend backend(environment);
    const auto reset = [&]
    {
        Reset();
        g_native_mode = g_native_spawns = g_native_removes = 0;
        g_native_create_callback = g_native_spawn_callback = {};
        environment.ready = KEEL_RESULT_OK;
        environment.system = EntitySystem();
        environment.on_base = environment.on_target = {};
        ++environment.epoch;
    };
    IdentityType identity{};
    reset();
    g_native_mode = 1;

    if (backend.Create("prop_dynamic", identity) != KEEL_RESULT_ALREADY_EXISTS || identity.epoch || g_native_removes ||
        !Identity()->m_pInstance)
        return 1201;

    for (unsigned mode : {2u,6u}) {
        reset();
        g_native_mode = mode;

        if (backend.Create("prop_dynamic",identity) != (mode == 2 ? KEEL_RESULT_INCOMPATIBLE : KEEL_RESULT_NOT_FOUND) ||
            identity.epoch || g_native_removes != 1 || Identity()->m_pInstance) return 1202;
    }

    reset();
    g_native_create_callback = [&]
    {
        ++environment.epoch;
        environment.system = reinterpret_cast<void*>(1);
    };

    if (backend.Create("prop_dynamic", identity) != KEEL_RESULT_NOT_FOUND || identity.epoch || g_native_removes)
        return 1203;

    reset();
    environment.on_base = [&]
    {
        ++environment.epoch;
    };

    if (backend.Create("prop_dynamic", identity) != KEEL_RESULT_OK || identity.epoch != environment.epoch ||
        backend.Validate(identity) != KEEL_RESULT_OK)
        return 1204;

    environment.on_base = {};
    Identity()->m_flags = static_cast<EntityFlags_t>(0);

    if (backend.Cancel(identity) != KEEL_RESULT_NOT_FOUND || g_native_removes)
        return 1205;

    for (unsigned mode : {0u,3u,4u,5u}) {
        reset();
        g_native_mode = mode;
        Store store(backend);
        std::uint64_t token{};
        KeelBool invoked{};

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
            return 1206;

        const auto result = store.Spawn(token,invoked);

        if (!invoked || !g_native_values_valid || store.Count() || g_native_spawns != 1 ||
            result != (mode == 0 ? KEEL_RESULT_OK : mode == 5 ? KEEL_RESULT_BUSY : KEEL_RESULT_ENGINE_FAILURE) ||
            g_native_removes != ((mode == 3 || mode == 4) ? 1u : 0u) ||
            store.Spawn(token,invoked) != KEEL_RESULT_NOT_FOUND || invoked) return 1207;
    }

    for (unsigned mode : {0u,3u}) {
        reset();
        Store store(backend);
        std::uint64_t token{};
        KeelBool invoked{};

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
            return 1208;

        g_native_spawn_callback = [&]
        {
            if (store.Cancel(token) != KEEL_RESULT_OK)
                throw std::runtime_error("close");
        };
        g_native_mode = mode;

        if (store.Spawn(token,invoked) != (mode == 0 ? KEEL_RESULT_OK : KEEL_RESULT_ENGINE_FAILURE) ||
            !invoked || store.Count() || g_native_removes != (mode == 0 ? 0u : 1u)) return 1209;
    }

    reset();
    {
        Store store(backend);
        std::uint64_t token{};
        KeelBool invoked{};

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
            return 1210;

        g_native_spawn_callback = [&]
        {
            ++environment.epoch;
            environment.system = reinterpret_cast<void*>(1);
            store.Reset();
        };
        g_native_mode = 3;

        if (store.Spawn(token, invoked) != KEEL_RESULT_NOT_FOUND || !invoked || store.Count() || g_native_removes)
            return 1211;
    }

    reset();
    {
        Store store(backend);
        std::uint64_t token{};
        g_native_create_callback = [&]
        {
            store.Reset();
        };

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_NOT_FOUND || token || store.Count() ||
            g_native_removes != 1)
            return 1212;
    }

    reset();
    {
        Store store(backend);
        std::uint64_t token{};

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
            return 1213;

        struct VisitContext
        {
            Store* store;
            std::uint64_t token;
        } context{&store, token};
        const auto callback = [](void* data, void* const* pointers, std::uint32_t count) -> KeelResult {
            auto& context = *static_cast<VisitContext*>(data);

            if (count != 1 || pointers[0] != EntityInstance() ||
                context.store->Cancel(context.token) != KEEL_RESULT_OK || !Identity()->m_pInstance)
                return KEEL_RESULT_ENGINE_FAILURE;

            return KEEL_RESULT_ALREADY_EXISTS;
        };

        if (store.Visit(token, "CCSPlayerPawn", callback, &context) != KEEL_RESULT_ALREADY_EXISTS || store.Count() ||
            g_native_removes != 1)
            return 1214;
    }

    reset();
    {
        Store store(backend);
        std::uint64_t token{};

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
            return 1215;

        StorePointer(g_entity_storage.data(),environment.table.data());
        KeelEntityTeleport request{sizeof(request), 7, {1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
        g_tool_calls = 0;
        g_tool_destroy = g_tool_throw = false;

        if (store.Teleport(token, request) != KEEL_RESULT_OK || g_tool_calls != 1 || !g_tool_arguments)
            return 1216;

        environment.on_target = [&]
        {
            ++environment.epoch;
            environment.system = reinterpret_cast<void*>(1);
        };

        if (store.Teleport(token,request) != KEEL_RESULT_NOT_FOUND || g_tool_calls != 1 ||
            store.Cancel(token) != KEEL_RESULT_NOT_FOUND || store.Count() || g_native_removes) return 1217;
    }

    reset();

    if (backend.Create("prop_dynamic", identity) != KEEL_RESULT_OK)
        return 1218;

    for (auto flag : {EF_SPAWN_IN_PROGRESS,EF_IS_ANONYMOUS_ALLOCATION}) {
        Identity()->m_flags = static_cast<EntityFlags_t>(EF_IS_PRE_SPAWN | flag);

        if (backend.Validate(identity) != KEEL_RESULT_NOT_FOUND || backend.Cancel(identity) != KEEL_RESULT_NOT_FOUND ||
            g_native_removes)
            return 1219;

        const KeelCs2EntityIdentity native{identity.index,identity.source2_handle};
        const auto bindings = environment.Bindings();
        KeelBool invoked = KEEL_TRUE;

        if (KeelCs2_RemoveCreatedEntity(EntitySystem(), &native, &bindings, &invoked) !=
                (flag == EF_SPAWN_IN_PROGRESS ? KEEL_RESULT_BUSY : KEEL_RESULT_INCOMPATIBLE) ||
            invoked || g_native_removes)
            return 1228;
    }

    Identity()->m_flags = EF_IS_PRE_SPAWN;
    environment.ready = KEEL_RESULT_WRONG_THREAD;

    if (backend.Cancel(identity) != KEEL_RESULT_WRONG_THREAD ||
        backend.Validate(identity) != KEEL_RESULT_WRONG_THREAD || g_native_removes)
        return 1220;

    reset();
    std::vector<std::uint32_t> handles(KEELS2_CS2_ENTITY_CAPACITY);

    if (KeelCs2_SnapshotEntityHandles(nullptr, handles.data(), static_cast<unsigned>(handles.size())) !=
            KEEL_RESULT_INVALID_ARGUMENT ||
        KeelCs2_SnapshotEntityHandles(EntitySystem(), handles.data(), 1) != KEEL_RESULT_INVALID_ARGUMENT ||
        KeelCs2_SnapshotEntityHandles(EntitySystem(), handles.data(), static_cast<unsigned>(handles.size())) !=
            KEEL_RESULT_OK ||
        handles[kEntityIndex] != static_cast<unsigned>(Identity()->GetRefEHandle().ToInt()) ||
        handles.back() != UINT32_MAX)
        return 1221;

    if (KeelFixtureKeyValuesMemoryCount())
        return 1222;

    reset();
    {
        Store store(backend);
        std::uint64_t token{};
        KeelBool invoked{};

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
            return 1223;

        g_native_mode = 7;

        if (store.Spawn(token,invoked) != KEEL_RESULT_ENGINE_FAILURE || !invoked || store.Count() ||
            g_native_removes != 1 || !KeelFixtureKeyValuesMemoryCount() || !g_native_retained ||
            KeelFixtureKeyValuesInspect(g_native_retained,"prop_dynamic",nullptr,0)) return 1224;

        KeelFixtureKeyValuesRelease(g_native_retained);
        g_native_retained = nullptr;
    }

    for (bool replace : {false,true}) {
        reset();
        Store store(backend);
        std::uint64_t token{};
        KeelBool invoked{};

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
            return 1225;

        auto* allocated = static_cast<std::byte*>(::operator new(kEntitySize, std::align_val_t{16}));
        std::memcpy(allocated,g_entity_storage.data(),kEntitySize);
        Identity()->m_pInstance = reinterpret_cast<CEntityInstance*>(allocated);
        g_native_mode = 3;
        g_native_spawn_callback = [&]
        {
            ::operator delete(allocated, std::align_val_t{16});
            Identity()->m_pInstance = replace ? EntityInstance() : nullptr;

            if (replace)
                SetHandle(*Identity(), 14);
        };

        if (store.Spawn(token,invoked) != KEEL_RESULT_NOT_FOUND || !invoked || store.Count() || g_native_removes ||
            (replace && Identity()->m_pInstance != EntityInstance())) return 1226;
    }

    reset();

    if (KeelFixtureKeyValuesMemoryCount())
        return 1227;

    {
        Store store(backend);
        std::uint64_t token{};
        KeelBool invoked{};

        if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
            return 1229;

        g_native_spawn_callback = [&] {
            Identity()->m_flags = static_cast<EntityFlags_t>(0);
            static_cast<void>(store.Cancel(token));
            throw std::runtime_error("after completed spawn");
        };

        if (store.Spawn(token,invoked) != KEEL_RESULT_ENGINE_FAILURE || !invoked || store.Count() ||
            !Identity()->m_pInstance || g_native_removes) return 1230;
    }

    reset();

    if (KeelFixtureKeyValuesMemoryCount())
        return 1231;

    {
        Store store(backend);
        std::uint64_t token{}, nested_token{};
        IdentityType nested{};
        g_native_mode = 8;
        g_native_create_callback = [&]
        {
            g_native_mode = 0;
            g_native_create_callback = {};

            if (store.Create("prop_dynamic", nested_token, nested) != KEEL_RESULT_OK)
                throw std::runtime_error("nested factory");
        };

        if (store.Create("prop_dynamic",token,identity) != KEEL_RESULT_ALREADY_EXISTS || token || identity.epoch ||
            store.Count() != 1 || g_native_removes || store.Describe(nested_token,identity) != KEEL_RESULT_OK ||
            identity.source2_handle != nested.source2_handle || store.Cancel(nested_token) != KEEL_RESULT_OK ||
            g_native_removes != 1) return 1232;
    }

    reset();

    if (KeelFixtureKeyValuesMemoryCount())
        return 1233;
    // Read-only pre-spawn observation nests without permitting mutation or
    // early deletion, including close/reset from the innermost callback.
    for (unsigned close_mode : {0u,1u,2u}) for (unsigned spawn_mode : {0u,3u}) {
            reset();
            Store store(backend);
            std::uint64_t token{};
            KeelBool invoked{};

            if (store.Create("prop_dynamic", token, identity) != KEEL_RESULT_OK)
                return 1234;

            struct Context
            {
                Store& store;
                std::uint64_t token;
                unsigned close_mode, depth{}, calls{};
                bool valid{true};

                static KeelResult Visit(void* data, void* const* pointers, std::uint32_t count)
                {
                    auto& c = *static_cast<Context*>(data);
                    ++c.depth;
                    ++c.calls;
                    c.valid = c.valid && count == 1 && pointers[0] == EntityInstance() && !g_native_removes;
                    const auto nested = c.store.Visit(c.token, "CCSPlayerPawn", &Visit, &c);
                    c.valid = c.valid && nested == (c.depth == 7 ? KEEL_RESULT_BUSY : KEEL_RESULT_OK);

                    if (c.depth == 7)
                    {
                        if (c.close_mode == 1)
                            c.valid = c.store.Cancel(c.token) == KEEL_RESULT_OK && c.valid;

                        if (c.close_mode == 2)
                            c.store.Reset();
                    }

                    c.valid = c.valid && !g_native_removes && Identity()->m_pInstance;
                    --c.depth;
                    return KEEL_RESULT_OK;
                }
        } context{store,token,close_mode};
        g_native_mode = spawn_mode;
        g_native_spawn_callback = [&] {
            context.valid = store.Visit(token,"CCSPlayerPawn",&Context::Visit,&context) == KEEL_RESULT_OK && context.valid;
            KeelBool nested{};
            context.valid = context.valid &&
                            store.Spawn(token, nested) == (close_mode ? KEEL_RESULT_NOT_FOUND : KEEL_RESULT_BUSY) &&
                            !nested;

            KeelCs2EntityKeyValue value{};
            value.size = sizeof(value);
            value.name = "spawnflags";
            value.type = KEELS2_CS2_KEY_INT32;
            context.valid = context.valid && store.Set(token,value) == (close_mode ? KEEL_RESULT_NOT_FOUND : KEEL_RESULT_BUSY);
            context.valid = context.valid && !g_native_removes;
        };

        if (store.Spawn(token,invoked) != (spawn_mode ? KEEL_RESULT_ENGINE_FAILURE : KEEL_RESULT_OK) || !invoked ||
            !context.valid || context.calls != 7 || store.Count() || g_native_removes != (spawn_mode ? 1u : 0u)) return 1235;
    }

    reset();

    if (KeelFixtureKeyValuesMemoryCount())
        return 1236;

    return 0;
}
