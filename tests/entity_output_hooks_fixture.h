// Included inside the native schema fixture's anonymous namespace.
struct OutputHookProbe final : keels2::cs2::OutputEnvironment {
    inline static OutputHookProbe* active{};
    keels2::cs2::NativeOutputHooks hooks{*this};
    KeelHookApi api{};
    KeelHookCallbackSpec installed{};
    std::uint64_t epoch{1};
    KeelResult readiness{KEEL_RESULT_OK}, defer_result{KEEL_RESULT_OK}, add_result{KEEL_RESULT_OK},
        remove_result{KEEL_RESULT_OK}, release_result{KEEL_RESULT_OK};
    unsigned pre{}, post{}, completed{}, original{}, resolves{}, removes{}, releases{};
    bool failed{}, block{}, skip_post{}, throwing{};
    KeelEntityOutputEvent observed{};
    std::vector<std::uint64_t> tokens;
    std::vector<std::pair<void (*)(void*),void*>> cleanups;
    std::function<void()> before, engine;
    void* value{};
    void* output{};
    OutputHookProbe() {
        active = this; api.size = sizeof(api); api.api_version = KEELHOOK_API_VERSION;
        api.resolve_target = &Resolve; api.add_callback = &Add;
        api.remove_callback = &Remove; api.release_target = &Release;
    }
    void Require(bool valid) { failed |= !valid; }
    KeelResult OutputCurrent(std::uint64_t expected, void*& system, std::uint64_t& current) noexcept override {
        system = EntitySystem(); current = epoch;
        return readiness != KEEL_RESULT_OK ? readiness : expected && expected != epoch ? KEEL_RESULT_NOT_FOUND : KEEL_RESULT_OK;
    }
    static KeelResult Resolve(KeelPluginHandle owner, const KeelHookTargetSpec* spec,
        const KeelHookPrototype* prototype, KeelHookTargetHandle* target) {
        auto& p = *active; ++p.resolves;
        p.Require(!owner && spec->source == KH_TARGET_ADDRESS && spec->mechanism == KH_MECHANISM_DETOUR && !spec->profile);
        p.Require(prototype->argument_count == 7 && prototype->return_type == KH_VALUE_VOID);
        for (unsigned i = 0; i < 7; ++i) p.Require(prototype->argument_types[i] == (i == 4 ? KH_VALUE_FLOAT32 : KH_VALUE_POINTER));
        *target = 77; return KEEL_RESULT_OK;
    }
    static KeelResult Add(KeelPluginHandle owner, KeelHookTargetHandle target, const KeelHookCallbackSpec* spec, KeelHookCallbackHandle* handle) {
        auto& p = *active; p.Require(!owner && target == 77 && spec->phases == KH_PHASE_BOTH);
        if (p.add_result != KEEL_RESULT_OK) return p.add_result;
        p.installed = *spec; *handle = 88; return KEEL_RESULT_OK;
    }
    static KeelResult Remove(KeelPluginHandle owner, KeelHookCallbackHandle handle) {
        auto& p = *active; ++p.removes; p.Require(!owner && handle == 88);
        if (p.remove_result == KEEL_RESULT_OK) p.installed = {};
        return p.remove_result;
    }
    static KeelResult Release(KeelPluginHandle owner, KeelHookTargetHandle target) {
        auto& p = *active; ++p.releases; p.Require(!owner && target == 77); return p.release_result;
    }
    static KeelResult Defer(KeelHookFrame*, void (*cleanup)(void*), void* data) noexcept {
        if (active->defer_result != KEEL_RESULT_OK) return active->defer_result;
        try { active->cleanups.emplace_back(cleanup,data); return KEEL_RESULT_OK; }
        catch (...) { return KEEL_RESULT_BUSY; }
    }
    static std::uint32_t Notify(const KeelEntityOutputEvent* event, std::uint64_t token, void* data) {
        auto& p = *static_cast<OutputHookProbe*>(data);
        p.Require(p.hooks.Stop() == KEEL_RESULT_BUSY);
        if (!event) {
            ++p.completed;
            const auto found = std::find(p.tokens.begin(),p.tokens.end(),token);
            p.Require(found != p.tokens.end()); if (found != p.tokens.end()) p.tokens.erase(found);
            return KEELS2_OUTPUT_CONTINUE;
        }
        p.Require(event->size == sizeof(*event) && event->delay == 1.25f && event->entity.index == kEntityIndex &&
            !std::strcmp(event->class_name,"trigger_multiple") && !std::strcmp(event->output_name,"OnTrigger") &&
            event->activator.index == -1 && !event->activator.epoch && event->caller.index == 4);
        p.observed = *event;
        if (event->phase == KEELS2_OUTPUT_PRE) {
            ++p.pre; p.Require(!event->flags && std::find(p.tokens.begin(),p.tokens.end(),token) == p.tokens.end());
            p.tokens.push_back(token);
            if (p.before) p.before();
            if (p.throwing) throw std::runtime_error("output observer failure");
            return p.block ? KEELS2_OUTPUT_BLOCK : KEELS2_OUTPUT_CONTINUE;
        }
        ++p.post; p.Require(event->phase == KEELS2_OUTPUT_POST && std::find(p.tokens.begin(),p.tokens.end(),token) != p.tokens.end());
        return KEELS2_OUTPUT_CONTINUE;
    }
    KeelResult Start() { return hooks.Start(FunctionAddress(&InputEntityFixture),"fixture",api,&Defer,&Notify,this); }
    void Fire(float delay = 1.25f) {
        const auto end = cleanups.size(); const auto callback = installed;
        std::array<KeelHookValue,7> args{};
        for (auto& arg : args) arg.type = KH_VALUE_POINTER;
        args[0].scalar.pointer = output; args[2].scalar.pointer = g_controller_storage.data();
        args[3].scalar.pointer = value; args[4].type = KH_VALUE_FLOAT32; args[4].scalar.float32 = delay;
        args[5].scalar.pointer = reinterpret_cast<void*>(5); args[6].scalar.pointer = reinterpret_cast<void*>(6);
        KeelHookFrame frame{sizeof(frame),KH_PHASE_PRE,77,7,0,args.data(),{}};
        const auto action = callback.callback(&frame,callback.user_data);
        Require(args[3].scalar.pointer == value && args[5].scalar.pointer == reinterpret_cast<void*>(5) &&
            args[6].scalar.pointer == reinterpret_cast<void*>(6));
        if (action == KH_ACTION_CONTINUE) { ++original; frame.flags = KH_FRAME_ORIGINAL_CALLED; if (engine) engine(); }
        else Require(action == KH_ACTION_SUPERSEDE);
        frame.phase = KH_PHASE_POST;
        if (!skip_post) Require(callback.callback(&frame,callback.user_data) == KH_ACTION_CONTINUE);
        while (cleanups.size() != end) {
            const auto entry = cleanups.back(); cleanups.pop_back(); entry.first(entry.second);
        }
    }
};
int RunOutputHookChecks()
{
    InputFixture(0); KeelFixtureKeyValuesMemoryStart();
    constexpr auto offset = Align(kHealthOffset,alignof(void*));
    struct Descriptor { const char* name; } descriptor{"OnTrigger"};
    struct Output { void* vtable; void* connections; const void* descriptor; } output{nullptr,nullptr,&descriptor};
    auto probe = std::make_unique<OutputHookProbe>(); auto& p = *probe;
    const auto reset = [&] {
        InputFixture(0); Identity()->m_designerName = CUtlSymbolLarge("trigger_multiple");
        std::memcpy(g_entity_storage.data()+offset,&output,sizeof(output)); p.output = g_entity_storage.data()+offset;
        p.pre = p.post = p.completed = p.original = 0; p.before = {}; p.engine = {}; p.block = p.skip_post = p.throwing = false;
    };
    reset(); KeelCs2VariantValue value{}; value.size = sizeof(value); value.type = KEELS2_INPUT_STRING; value.string_value = "copied output";
    if (KeelCs2Variant_Build(&value,&p.value) != KEEL_RESULT_OK) return 1500;
    if (p.Start() != KEEL_RESULT_OK || !p.hooks.Started() || p.Start() != KEEL_RESULT_OK || p.resolves != 1) return 1501;
    if (p.hooks.Start(FunctionAddress(&QueueEntityInputFixture),"fixture",p.api,&OutputHookProbe::Defer,&OutputHookProbe::Notify,&p) != KEEL_RESULT_BUSY) return 1519;
    p.Fire();
    if (p.pre != 1 || p.post != 1 || p.completed != 1 || p.original != 1 || !p.tokens.empty() ||
        p.observed.flags != KEELS2_OUTPUT_ORIGINAL_CALLED || p.observed.value_status != KEEL_RESULT_OK ||
        std::strcmp(p.observed.value.string_value,"copied output")) return 1502;
    reset(); p.block = true; p.Fire();
    if (p.pre != 1 || p.post != 1 || p.completed != 1 || p.original || p.observed.flags) return 1503;
    reset(); p.throwing = true; p.Fire();
    if (p.pre != 1 || p.post != 1 || p.completed != 1 || p.original || p.observed.flags) return 1504;
    reset(); p.skip_post = true; p.Fire();
    if (p.pre != 1 || p.post || p.completed != 1 || !p.tokens.empty()) return 1505;
    reset(); p.before = [&] { ++p.epoch; }; p.Fire();
    if (p.original || p.observed.flags || p.completed != 1) return 1506;
    reset(); p.before = [&] { Identity()->m_pInstance = nullptr; }; p.Fire();
    if (p.original || p.observed.flags || p.completed != 1) return 1507;
    reset(); p.engine = [&] { KeelCs2Variant_Release(p.value); p.value = nullptr; g_entity_storage.fill(std::byte{0xdd}); Identity()->m_pInstance = nullptr; };
    p.Fire();
    if (p.post != 1 || p.completed != 1 || p.observed.flags != KEELS2_OUTPUT_ORIGINAL_CALLED ||
        std::strcmp(p.observed.value.string_value,"copied output")) return 1508;
    reset(); p.Fire();
    if (p.pre != 1 || p.observed.value_status != KEEL_RESULT_INVALID_ARGUMENT || p.observed.value.type != UINT32_MAX) return 1509;
    reset(); p.before = [&] { if (p.pre <= 64) p.Fire(); }; p.Fire();
    if (p.pre != 64 || p.post != 64 || p.completed != 64 || p.original != 65 || !p.tokens.empty()) return 1510;
    reset(); p.defer_result = KEEL_RESULT_BUSY; p.Fire(); p.defer_result = KEEL_RESULT_OK;
    if (p.pre || p.post || p.completed || p.original != 1) return 1511;
    reset(); p.readiness = KEEL_RESULT_NOT_READY; p.Fire(); p.readiness = KEEL_RESULT_OK;
    if (p.pre || p.completed || p.original != 1) return 1512;
    reset(); p.Fire(std::numeric_limits<float>::infinity());
    if (p.pre || p.completed || p.original != 1) return 1513;
    p.remove_result = KEEL_RESULT_BUSY;
    if (p.hooks.Stop() != KEEL_RESULT_BUSY || !p.hooks.Started()) return 1514;
    p.remove_result = KEEL_RESULT_OK; p.release_result = KEEL_RESULT_BUSY;
    if (p.hooks.Stop() != KEEL_RESULT_BUSY || p.hooks.Started() || p.Start() != KEEL_RESULT_BUSY) return 1515;
    p.release_result = KEEL_RESULT_OK;
    if (p.hooks.Stop() != KEEL_RESULT_OK || p.Start() != KEEL_RESULT_OK || p.hooks.Stop() != KEEL_RESULT_OK) return 1516;
    p.add_result = KEEL_RESULT_ENGINE_FAILURE; p.release_result = KEEL_RESULT_BUSY;
    if (p.Start() != KEEL_RESULT_ENGINE_FAILURE || p.hooks.Started() || p.Start() != KEEL_RESULT_BUSY) return 1517;
    p.release_result = KEEL_RESULT_OK;
    if (p.hooks.Stop() != KEEL_RESULT_OK || p.failed || !p.tokens.empty() || KeelFixtureKeyValuesMemoryCount()) return 1518;
    KeelFixtureKeyValuesMemoryStop(); g_input_handle.Get()->m_sTypeName.Purge(); Reset(); OutputHookProbe::active = nullptr;
    return 0;
}
