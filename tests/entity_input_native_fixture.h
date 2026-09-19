// Included inside the native schema fixture's anonymous namespace.
unsigned g_io_calls{}, g_io_mode{};
bool g_io_arguments{};
void* g_io_copy{};
char* g_io_source{};
void* g_io_activator{};
void* g_io_caller{};
KeelCs2VariantValue g_io_expected{};
void InputEntityFixture(void* target, const char* input, void* activator, void* caller, const void* value)
{
    ++g_io_calls;
    if (g_io_source) g_io_source[0] = 'X';
    g_io_arguments = target == Identity()->m_pInstance && !std::strcmp(input,"Enable") &&
        activator == g_io_activator && caller == g_io_caller &&
        KeelFixtureVariantInspect(const_cast<void*>(value),&g_io_expected) == 0;
    if (g_io_mode == 1 || g_io_mode == 2) {
        Identity()->m_pInstance = nullptr;
        if (target != EntityInstance()) ::operator delete(target,std::align_val_t{16});
        g_entity_storage.fill(std::byte{0xdd});
    }
    if (g_io_mode == 2) throw std::runtime_error("input callback failure");
}
void QueueEntityInputFixture(void* system, void* target, const char* input, void* activator, void* caller,
    const void* value, float delay, void* context, void* kv)
{
    InputEntityFixture(target,input,activator,caller,value);
    g_io_arguments = g_io_arguments && system == EntitySystem() && delay == 1.25f && !context && !kv;
    g_io_copy = KeelFixtureVariantCopy(const_cast<void*>(value));
}
int RunEntityInputChecks()
{
    InputFixture(0); KeelFixtureKeyValuesMemoryStart();
    KeelCs2EntityInputBindings bindings{FunctionAddress(&InputEntityFixture),FunctionAddress(&QueueEntityInputFixture)};
    KeelCs2EntityIdentity target{}, controller{};
    if (KeelCs2_FindEntityByIndex(EntitySystem(),kEntityIndex,&target) != KEEL_RESULT_OK ||
        KeelCs2_FindEntityByIndex(EntitySystem(),4,&controller) != KEEL_RESULT_OK) return 1300;
    g_io_mode = g_io_calls = 0; g_io_source = nullptr;
    g_io_activator = g_controller_storage.data(); g_io_caller = EntityInstance();
    void* value{}; KeelBool invoked{};
    for (unsigned type = 0; type <= 8; ++type) {
        g_io_expected = {}; g_io_expected.size = sizeof(g_io_expected); g_io_expected.type = type;
        g_io_expected.string_value = "input value"; g_io_expected.int_value = 1; g_io_expected.float_value = 2.5f;
        g_io_expected.vector_value[0] = 5; g_io_expected.vector_value[1] = -90; g_io_expected.vector_value[2] = 17;
        g_io_expected.color_value[0] = 55; g_io_expected.color_value[3] = 255;
        g_io_expected.entity_handle = controller.source2_handle;
        if (KeelCs2Variant_Build(&g_io_expected,&value) != KEEL_RESULT_OK) return 1301;
        const auto* value_entity = type == 8 ? &controller : nullptr;
        char name[] = "Enable"; g_io_source = name;
        auto result = KeelCs2_EntityInput(EntitySystem(),&target,&controller,&target,value_entity,&bindings,name,value,KEEL_FALSE,0,&invoked);
        if (result != KEEL_RESULT_OK || !invoked || !g_io_arguments || name[0] != 'X') return 1302;
        g_io_source = nullptr; const auto before = g_io_calls;
        result = KeelCs2_EntityInput(EntitySystem(),&target,&controller,&target,value_entity,&bindings,"Enable",value,KEEL_TRUE,1.25f,&invoked);
        if (type == 7) {
            if (result != KEEL_RESULT_UNSUPPORTED || invoked || g_io_calls != before || g_io_copy) return 1303;
        } else if (result != KEEL_RESULT_OK || !invoked || !g_io_arguments || !g_io_copy) return 1304;
        KeelCs2Variant_Release(value);
        if (g_io_copy) {
            if (KeelFixtureVariantInspect(g_io_copy,&g_io_expected)) return 1305;
            KeelFixtureVariantRelease(g_io_copy); g_io_copy = nullptr;
        }
        if (KeelFixtureKeyValuesMemoryCount()) return 1306;
    }
    g_io_expected = {}; g_io_expected.size = sizeof(g_io_expected);
    if (KeelCs2Variant_Build(&g_io_expected,&value) != KEEL_RESULT_OK) return 1307;
    const auto call = [&](const KeelCs2EntityIdentity* entity, const KeelCs2EntityIdentity* activator,
        const KeelCs2EntityIdentity* caller, const KeelCs2EntityIdentity* payload, const char* name,
        KeelBool queued = KEEL_FALSE, float delay = 0.0f) {
        invoked = KEEL_TRUE;
        return KeelCs2_EntityInput(EntitySystem(),entity,activator,caller,payload,&bindings,name,value,queued,delay,&invoked);
    };
    const auto before = g_io_calls;
    auto stale = controller; stale.source2_handle += 0x8000;
    if (call(&target,&stale,nullptr,nullptr,"Enable") != KEEL_RESULT_NOT_FOUND || invoked ||
        call(&target,nullptr,&stale,nullptr,"Enable") != KEEL_RESULT_NOT_FOUND || invoked ||
        call(&stale,nullptr,nullptr,nullptr,"Enable") != KEEL_RESULT_NOT_FOUND || invoked) return 1308;
    auto mismatch = target; ++mismatch.index;
    if (call(&mismatch,nullptr,nullptr,nullptr,"Enable") != KEEL_RESULT_NOT_FOUND || invoked) return 1309;
    for (auto flag : {EF_IS_PRE_SPAWN,EF_IS_CONSTRUCTION_IN_PROGRESS,EF_MARKED_FOR_DELETE,EF_DELETE_IN_PROGRESS,EF_IS_INVALID_EHANDLE}) {
        Identity()->m_flags = flag;
        if (call(&target,nullptr,nullptr,nullptr,"Enable") != KEEL_RESULT_NOT_FOUND || invoked) return 1310;
    }
    Identity()->m_flags = static_cast<EntityFlags_t>(0);
    std::string long_name(128,'n');
    for (const char* name : {"","bad\nname",long_name.c_str(),static_cast<const char*>(nullptr)})
        if (call(&target,nullptr,nullptr,nullptr,name) != KEEL_RESULT_INVALID_ARGUMENT || invoked) return 1311;
    for (float delay : {-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        if (call(&target,nullptr,nullptr,nullptr,"Enable",KEEL_TRUE,delay) != KEEL_RESULT_INVALID_ARGUMENT || invoked) return 1312;
    if (call(&target,nullptr,nullptr,nullptr,"Enable",2) != KEEL_RESULT_INVALID_ARGUMENT || invoked ||
        call(&target,nullptr,nullptr,nullptr,"Enable",KEEL_FALSE,1) != KEEL_RESULT_INVALID_ARGUMENT || invoked ||
        call(nullptr,nullptr,nullptr,nullptr,"Enable") != KEEL_RESULT_INVALID_ARGUMENT || invoked ||
        call(&target,nullptr,nullptr,&controller,"Enable") != KEEL_RESULT_INVALID_ARGUMENT || invoked || g_io_calls != before) return 1313;
    g_io_activator = g_io_caller = nullptr;
    if (call(&target,nullptr,nullptr,nullptr,"Enable") != KEEL_RESULT_OK || !invoked || !g_io_arguments) return 1314;
    KeelCs2Variant_Release(value);
    g_io_expected.type = 8; g_io_expected.entity_handle = controller.source2_handle;
    if (KeelCs2Variant_Build(&g_io_expected,&value) != KEEL_RESULT_OK) return 1315;
    if (call(&target,nullptr,nullptr,nullptr,"Enable") != KEEL_RESULT_INVALID_ARGUMENT || invoked ||
        call(&target,nullptr,nullptr,&target,"Enable") != KEEL_RESULT_INVALID_ARGUMENT || invoked) return 1316;
    auto* controller_identity = &reinterpret_cast<CEntityIdentity*>(g_identity_storage.data())[4];
    controller_identity->m_flags = EF_MARKED_FOR_DELETE;
    if (call(&target,nullptr,nullptr,&controller,"Enable") != KEEL_RESULT_NOT_FOUND || invoked) return 1317;
    controller_identity->m_flags = static_cast<EntityFlags_t>(0);
    KeelCs2Variant_Release(value);
    g_io_expected.type = 0;
    if (KeelCs2Variant_Build(&g_io_expected,&value) != KEEL_RESULT_OK) return 1318;
    const auto valid_bindings = bindings;
    bindings.accept = nullptr;
    if (call(&target,nullptr,nullptr,nullptr,"Enable") != KEEL_RESULT_INVALID_ARGUMENT || invoked) return 1321;
    bindings = valid_bindings; bindings.queue = nullptr;
    if (call(&target,nullptr,nullptr,nullptr,"Enable") != KEEL_RESULT_INVALID_ARGUMENT || invoked) return 1322;
    bindings = valid_bindings;
    if (KeelCs2_EntityInput(EntitySystem(),&target,nullptr,nullptr,nullptr,&bindings,"Enable",nullptr,0,0,&invoked) !=
        KEEL_RESULT_INVALID_ARGUMENT || invoked ||
        KeelCs2_EntityInput(EntitySystem(),&target,nullptr,nullptr,nullptr,&bindings,"Enable",value,0,0,nullptr) !=
        KEEL_RESULT_INVALID_ARGUMENT) return 1323;
    for (KeelBool queued : {KEEL_FALSE,KEEL_TRUE}) for (unsigned mode : {1u,2u}) {
        Reset(); KeelCs2_FindEntityByIndex(EntitySystem(),kEntityIndex,&target);
        void* dynamic = ::operator new(kEntitySize,std::align_val_t{16});
        std::memcpy(dynamic,g_entity_storage.data(),kEntitySize); Identity()->m_pInstance = static_cast<CEntityInstance*>(dynamic);
        g_io_mode = mode;
        if (call(&target,nullptr,nullptr,nullptr,"Enable",queued,queued ? 1.25f : 0.0f) != (mode == 1 ? KEEL_RESULT_OK : KEEL_RESULT_ENGINE_FAILURE) ||
            !invoked || !g_io_arguments) return 1319;
        if (g_io_copy) { KeelFixtureVariantRelease(g_io_copy); g_io_copy = nullptr; }
    }
    g_io_mode = 0; KeelCs2Variant_Release(value);
    if (KeelFixtureKeyValuesMemoryCount()) return 1320;
    KeelFixtureKeyValuesMemoryStop(); g_input_handle.Get()->m_sTypeName.Purge(); Reset();
    return 0;
}

class NativeInputEnvironment final : public keels2::cs2::InputEnvironment
{
public:
    std::uint64_t epoch{17};
    bool stopping{}, null_system{};
    unsigned acquisitions{};
    std::size_t allocations_at_acquisition{};
    KeelResult capabilities_result{KEEL_RESULT_OK};
    std::function<void()> on_capabilities;
    KeelResult InputCapabilities(std::uint32_t& direct, std::uint32_t& queued) override {
        const auto callback = on_capabilities; if (callback) callback();
        direct = 511; queued = 383; return capabilities_result;
    }
    KeelResult InputCurrent(std::uint64_t expected, void*& system, std::uint64_t& current) noexcept override {
        ++acquisitions; allocations_at_acquisition = KeelFixtureKeyValuesMemoryCount();
        system = nullptr; current = 0;
        if (stopping) return KEEL_RESULT_NOT_READY;
        if (epoch != expected) return KEEL_RESULT_NOT_FOUND;
        system = null_system ? nullptr : EntitySystem(); current = epoch; return KEEL_RESULT_OK;
    }
    KeelCs2EntityInputBindings InputBindings() const noexcept override {
        return {FunctionAddress(&InputEntityFixture),FunctionAddress(&QueueEntityInputFixture)};
    }
};
int RunNativeInputChecks()
{
    InputFixture(0); KeelFixtureKeyValuesMemoryStart();
    NativeInputEnvironment environment; keels2::cs2::NativeInputBackend backend(environment);
    KeelCs2EntityIdentity target{}, controller{};
    if (KeelCs2_FindEntityByIndex(EntitySystem(),kEntityIndex,&target) != KEEL_RESULT_OK ||
        KeelCs2_FindEntityByIndex(EntitySystem(),4,&controller) != KEEL_RESULT_OK) return 1330;
    keels2::host::GameEntityInputRequest request{};
    request.target = {target.index,target.source2_handle,environment.epoch};
    request.activator = {controller.index,controller.source2_handle,environment.epoch};
    request.caller = request.target;
    request.input = "Enable"; request.value.size = sizeof(request.value);
    g_io_mode = 0; g_io_source = nullptr; g_io_activator = g_controller_storage.data(); g_io_caller = EntityInstance();
    KeelBool invoked{};
    const auto call = [&] { invoked = KEEL_TRUE; return backend.Dispatch(request,invoked); };
    for (unsigned type = 0; type <= 8; ++type) {
        g_io_expected = {}; g_io_expected.size = sizeof(g_io_expected); g_io_expected.type = type;
        g_io_expected.string_value = "input value"; g_io_expected.int_value = 1; g_io_expected.float_value = -2.5f;
        g_io_expected.vector_value[1] = 17; g_io_expected.color_value[3] = 255;
        g_io_expected.entity_handle = controller.source2_handle;
        request.value.type = type; request.value.string_value = g_io_expected.string_value;
        request.value.int_value = g_io_expected.int_value; request.value.float_value = g_io_expected.float_value;
        std::memcpy(request.value.vector_value,g_io_expected.vector_value,sizeof(request.value.vector_value));
        std::memcpy(request.value.color_value,g_io_expected.color_value,sizeof(request.value.color_value));
        request.value_entity = type == KEELS2_INPUT_ENTITY ? request.activator : keels2::host::GameEntityIdentity{};
        request.queued = KEEL_FALSE; request.delay = 0;
        if (call() != KEEL_RESULT_OK || !invoked || !g_io_arguments || KeelFixtureKeyValuesMemoryCount()) return 1331;
        request.queued = KEEL_TRUE; request.delay = 1.25f; const auto before = g_io_calls;
        const auto result = call();
        if (type == KEELS2_INPUT_COLOR) {
            if (result != KEEL_RESULT_UNSUPPORTED || invoked || g_io_calls != before) return 1332;
        } else {
            if (result != KEEL_RESULT_OK || !invoked || !g_io_arguments || !g_io_copy ||
                KeelFixtureVariantInspect(g_io_copy,&g_io_expected)) return 1333;
            KeelFixtureVariantRelease(g_io_copy); g_io_copy = nullptr;
        }
        if (KeelFixtureKeyValuesMemoryCount()) return 1334;
    }
    request.queued = KEEL_FALSE; request.delay = 0; request.value_entity = {};
    char name[] = "Enable", text[] = "copied";
    request.input = name; request.value.type = KEELS2_INPUT_STRING; request.value.string_value = text;
    g_io_expected.type = KEELS2_INPUT_STRING; g_io_expected.string_value = "copied";
    environment.on_capabilities = [&] {
        name[0] = text[0] = 'X'; request.target = {}; request.value.type = KEELS2_INPUT_VOID;
    };
    if (call() != KEEL_RESULT_OK || !invoked || !g_io_arguments || environment.allocations_at_acquisition != 2 ||
        KeelFixtureKeyValuesMemoryCount()) return 1335;
    environment.on_capabilities = {};
    request.target = {target.index,target.source2_handle,environment.epoch}; request.input = "Enable";
    request.value.type = KEELS2_INPUT_STRING; request.value.string_value = "copied";
    const auto reject = [&](KeelResult expected) {
        const auto before = g_io_calls;
        return call() == expected && !invoked && g_io_calls == before && !KeelFixtureKeyValuesMemoryCount();
    };
    for (int after : {0,1}) for (bool throws : {false,true}) {
        KeelFixtureKeyValuesMemoryFailAfter(after,throws);
        const auto before = environment.acquisitions;
        const bool valid = reject(KEEL_RESULT_ENGINE_FAILURE);
        KeelFixtureKeyValuesMemoryFailAfter(-1,false);
        if (!valid || before != environment.acquisitions) return 1336;
    }
    environment.on_capabilities = [&] { ++environment.epoch; };
    if (!reject(KEEL_RESULT_NOT_FOUND)) return 1337;
    environment.on_capabilities = {}; --environment.epoch;
    environment.on_capabilities = [&] { environment.stopping = true; };
    if (!reject(KEEL_RESULT_NOT_READY)) return 1338;
    environment.on_capabilities = {}; environment.stopping = false;
    environment.null_system = true;
    if (!reject(KEEL_RESULT_NOT_FOUND)) return 1339;
    environment.null_system = false; environment.capabilities_result = KEEL_RESULT_UNSUPPORTED;
    if (!reject(KEEL_RESULT_UNSUPPORTED)) return 1340;
    environment.capabilities_result = KEEL_RESULT_OK;
    ++request.activator.epoch;
    if (!reject(KEEL_RESULT_NOT_FOUND)) return 1341;
    --request.activator.epoch; request.value_entity.index = 4;
    if (!reject(KEEL_RESULT_INVALID_ARGUMENT)) return 1342;
    request.value_entity = {};
    environment.on_capabilities = [&] { Identity()->m_flags = EF_MARKED_FOR_DELETE; };
    if (!reject(KEEL_RESULT_NOT_FOUND)) return 1343;
    environment.on_capabilities = {}; Identity()->m_flags = static_cast<EntityFlags_t>(0);
    g_io_mode = 2;
    if (call() != KEEL_RESULT_ENGINE_FAILURE || !invoked || !g_io_arguments || KeelFixtureKeyValuesMemoryCount()) return 1344;
    g_io_mode = 0;
    KeelFixtureKeyValuesMemoryStop(); g_input_handle.Get()->m_sTypeName.Purge(); Reset();
    return 0;
}
