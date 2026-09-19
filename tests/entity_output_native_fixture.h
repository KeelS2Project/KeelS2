// Included inside the native schema fixture's anonymous namespace.
int RunOutputContextChecks()
{
    constexpr auto offset = Align(kHealthOffset,alignof(void*));
    struct Output { const void* vtable; const void* connections; const void* descriptor; };
    struct Descriptor { const char* name; std::uint32_t flags; std::uint32_t unverified_offset; };
    static_assert(sizeof(Output) == 24 && offset+sizeof(Output) <= kEntitySize);
    Descriptor descriptor{"OnTrigger",UINT32_MAX,UINT32_MAX};
    const Output output{reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),&descriptor};
    const auto reset = [&] {
        InputFixture(0);
        Identity()->m_designerName = CUtlSymbolLarge("trigger_multiple");
        std::memcpy(g_entity_storage.data()+offset,&output,sizeof(output));
    };
    reset();
    KeelCs2EntityOutputContext context{};
    const void* address = g_entity_storage.data()+offset;
    const auto capture = [&](const void* pointer, const void* activator, const void* caller) {
        std::memset(&context,0xff,sizeof(context));
        return KeelCs2_CaptureOutputContext(EntitySystem(),pointer,activator,caller,&context);
    };
    const auto clear = [&] { const KeelCs2EntityOutputContext empty{}; return !std::memcmp(&context,&empty,sizeof(context)); };
    const auto valid = [&] { return KeelCs2_ValidateOutputContext(EntitySystem(),&context); };
    if (capture(address,g_controller_storage.data(),EntityInstance()) != KEEL_RESULT_OK || valid() != KEEL_RESULT_OK ||
        context.entities[0].index != kEntityIndex || context.entities[1].index != 4 || context.entities[2].index != kEntityIndex ||
        context.entities[0].source2_handle != static_cast<std::uint32_t>(Identity()->GetRefEHandle().ToInt()) ||
        std::strcmp(context.class_name,"trigger_multiple") || std::strcmp(context.schema_name,"CCSPlayerPawn") ||
        std::strcmp(context.output_name,"OnTrigger")) return 1400;
    if (capture(address,nullptr,g_controller_storage.data()) != KEEL_RESULT_OK || context.instances[1] ||
        context.entities[2].index != 4 || context.entities[0].index != kEntityIndex || valid() != KEEL_RESULT_OK) return 1401;
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK || context.instances[1] || context.instances[2] || valid() != KEEL_RESULT_OK) return 1402;
    for (const void* invalid : {static_cast<const void*>(nullptr),reinterpret_cast<const void*>(1)})
        if (capture(invalid,nullptr,nullptr) != KEEL_RESULT_INVALID_ARGUMENT || !clear()) return 1403;
    for (const void* invalid : {static_cast<const void*>(EntityInstance()),reinterpret_cast<const void*>(16),
            reinterpret_cast<const void*>(UINTPTR_MAX-7),static_cast<const void*>(g_entity_storage.data()+kEntitySize-16)})
        if (capture(invalid,nullptr,nullptr) != KEEL_RESULT_NOT_FOUND || !clear()) return 1404;
    if (capture(address,reinterpret_cast<void*>(16),nullptr) != KEEL_RESULT_NOT_FOUND || !clear() ||
        capture(address,nullptr,reinterpret_cast<void*>(16)) != KEEL_RESULT_NOT_FOUND || !clear()) return 1405;
    for (auto flags : {EF_IS_PRE_SPAWN,EF_SPAWN_IN_PROGRESS,static_cast<EntityFlags_t>(EF_IS_PRE_SPAWN | EF_SPAWN_IN_PROGRESS)}) {
        Identity()->m_flags = flags;
        if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK || valid() != KEEL_RESULT_OK) return 1406;
    }
    for (auto flags : {EF_IS_INVALID_EHANDLE,EF_MARKED_FOR_DELETE,EF_DELETE_IN_PROGRESS,EF_IS_CONSTRUCTION_IN_PROGRESS}) {
        reset(); if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK) return 1407;
        Identity()->m_flags = flags;
        if (valid() != KEEL_RESULT_NOT_FOUND || capture(address,nullptr,nullptr) != KEEL_RESULT_NOT_FOUND || !clear()) return 1408;
    }
    reset(); if (capture(address,g_controller_storage.data(),g_controller_storage.data()) != KEEL_RESULT_OK) return 1409;
    auto* controller = &reinterpret_cast<CEntityIdentity*>(g_identity_storage.data())[4];
    controller->m_flags = EF_MARKED_FOR_DELETE;
    if (valid() != KEEL_RESULT_NOT_FOUND) return 1410;
    reset(); if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK) return 1411;
    SetHandle(*Identity(),13);
    if (valid() != KEEL_RESULT_NOT_FOUND) return 1412;
    reset(); if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK) return 1413;
    Identity()->m_pInstance->m_pEntity = nullptr;
    if (valid() != KEEL_RESULT_NOT_FOUND || capture(address,nullptr,nullptr) != KEEL_RESULT_NOT_FOUND || !clear()) return 1414;
    reset(); if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK) return 1415;
    auto other_type = g_derived_class; g_entity_class_info.m_pSchemaBinding = &other_type;
    if (valid() != KEEL_RESULT_NOT_FOUND) return 1416;
    for (int size : {0,static_cast<int>(sizeof(CEntityInstance)),static_cast<int>(offset+23),16*1024*1024+1}) {
        reset(); if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK) return 1417;
        g_derived_class.m_nSize = size;
        if (valid() != KEEL_RESULT_NOT_FOUND || capture(address,nullptr,nullptr) != KEEL_RESULT_NOT_FOUND || !clear()) return 1418;
    }
    reset(); EntityClass()->m_pClassInfo = nullptr;
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_NOT_FOUND || !clear()) return 1419;
    reset(); g_derived_class.m_nAlignment = 0;
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_NOT_FOUND || !clear()) return 1420;
    reset();
    {
        std::string name("OnOriginal"), classname("original_class"), schema("COriginal");
        descriptor.name = name.c_str(); Identity()->m_designerName = CUtlSymbolLarge(classname.c_str());
        g_derived_class.m_pszName = schema.c_str();
        if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK) return 1421;
        name.assign(1024,'n'); classname.assign(1024,'c'); schema.assign(1024,'s');
    }
    if (valid() != KEEL_RESULT_OK || std::strcmp(context.output_name,"OnOriginal") ||
        std::strcmp(context.class_name,"original_class") || std::strcmp(context.schema_name,"COriginal")) return 1422;
    descriptor.name = "OnTrigger"; reset();
    std::string long_name(128,'n'); descriptor.name = long_name.c_str();
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_INCOMPATIBLE || !clear()) return 1423;
    long_name.pop_back(); descriptor.name = long_name.c_str();
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK || std::strlen(context.output_name) != 127) return 1424;
    for (const char* name : {"bad\nname","bad\x7f"}) {
        descriptor.name = name;
        if (capture(address,nullptr,nullptr) != KEEL_RESULT_INCOMPATIBLE || !clear()) return 1425;
    }
    descriptor.name = nullptr;
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK || context.output_name[0]) return 1426;
    const Output unnamed{}; std::memcpy(g_entity_storage.data()+offset,&unnamed,sizeof(unnamed));
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK || context.output_name[0]) return 1427;
    descriptor.name = "OnTrigger"; reset();
    std::string long_class(256,'c'); Identity()->m_designerName = CUtlSymbolLarge(long_class.c_str());
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_INCOMPATIBLE || !clear()) return 1428;
    long_class.pop_back(); Identity()->m_designerName = CUtlSymbolLarge(long_class.c_str());
    if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK || std::strlen(context.class_name) != 255) return 1429;
    reset();
    void* heap = ::operator new(kEntitySize,std::align_val_t{16});
    std::memcpy(heap,g_entity_storage.data(),kEntitySize); Identity()->m_pInstance = static_cast<CEntityInstance*>(heap);
    if (capture(static_cast<std::byte*>(heap)+offset,nullptr,nullptr) != KEEL_RESULT_OK) return 1430;
    Identity()->m_pInstance = nullptr; ::operator delete(heap,std::align_val_t{16});
    if (valid() != KEEL_RESULT_NOT_FOUND || std::strcmp(context.output_name,"OnTrigger")) return 1431;
    reset(); if (capture(address,nullptr,nullptr) != KEEL_RESULT_OK) return 1432;
    heap = ::operator new(kEntitySize,std::align_val_t{16});
    std::memcpy(heap,g_entity_storage.data(),kEntitySize); Identity()->m_pInstance = static_cast<CEntityInstance*>(heap);
    if (valid() != KEEL_RESULT_NOT_FOUND) return 1433;
    Identity()->m_pInstance = nullptr; ::operator delete(heap,std::align_val_t{16});
    reset();
    heap = ::operator new(3*kEntitySize,std::align_val_t{16});
    std::memcpy(heap,g_entity_storage.data(),kEntitySize);
    auto* nested = static_cast<std::byte*>(heap)+kEntitySize;
    std::memcpy(nested,g_controller_storage.data(),kEntitySize); std::memcpy(nested+offset,&output,sizeof(output));
    Identity()->m_pInstance = static_cast<CEntityInstance*>(heap);
    controller = &reinterpret_cast<CEntityIdentity*>(g_identity_storage.data())[4];
    controller->m_pInstance = reinterpret_cast<CEntityInstance*>(nested); g_derived_class.m_nSize = static_cast<int>(3*kEntitySize);
    if (capture(nested+offset,nullptr,nullptr) != KEEL_RESULT_INCOMPATIBLE || !clear()) return 1434;
    Identity()->m_pInstance = controller->m_pInstance = nullptr; ::operator delete(heap,std::align_val_t{16});
    g_input_handle.Get()->m_sTypeName.Purge(); Reset();
    return 0;
}
