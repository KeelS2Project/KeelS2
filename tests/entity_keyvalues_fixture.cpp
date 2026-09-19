#include <keels2/cs2/entity_keyvalues.h>
#include <entity2/entitykeyvalues.h>
#include <cstring>
#if defined(_WIN32)
#define KV_INSPECT_EXPORT __declspec(dllexport)
#else
#define KV_INSPECT_EXPORT __attribute__((visibility("default")))
#endif
static_assert(sizeof(CEntityKeyValues) == 0x38);
#if defined(_WIN32)
static_assert(sizeof(CUtlBuffer) == 64);
#else
static_assert(sizeof(CUtlBuffer) == 80);
#endif
extern "C" KV_INSPECT_EXPORT int KeelFixtureKeyValuesInspect(void* pointer, const char* class_name,
    const KeelCs2EntityKeyValue* expected, unsigned count)
{
    auto* value = static_cast<CEntityKeyValues*>(pointer);

    if (std::strcmp(value->GetString("classname"), class_name) || value->GetNumConnectionDescs())
        return 1;

    unsigned keys{};

    for (auto it = value->First(); value->IsValidIterator(it); it = value->Next(it)) {
        const char* name = value->GetEntityKeyId(it).GetString();
        bool found = !std::strcmp(name,"classname");

        for (unsigned i = 0; i < count; ++i)
            found = found || !std::strcmp(name, expected[i].name);

        if (!found || value->IsAttribute(it))
            return 12;

        ++keys;
    }

    if (keys != count + 1)
        return 2;

    for (unsigned i = 0; i < count; ++i) {
        const auto& e = expected[i];
        const EntityKeyId_t key(e.name);

        if (!value->HasValue(key) || value->HasAttribute(key))
            return 3;

        const auto* kv = value->GetKeyValue(key);

        switch (e.type) {
            case KEELS2_CS2_KEY_STRING:
                if (kv->GetType() != KV3_TYPE_STRING || std::strcmp(value->GetString(key), e.string_value))
                    return 4;

                break;

            case KEELS2_CS2_KEY_BOOL:
                if (kv->GetType() != KV3_TYPE_BOOL || value->GetBool(key) != (e.int_value != 0))
                    return 5;

                break;

            case KEELS2_CS2_KEY_INT32:
                if (kv->GetSubType() != KV3_SUBTYPE_INT32 || value->GetInt(key) != e.int_value)
                    return 6;

                break;

            case KEELS2_CS2_KEY_FLOAT:
                if (kv->GetSubType() != KV3_SUBTYPE_FLOAT32 || value->GetFloat(key) != e.float_value)
                    return 7;

                break;

            case KEELS2_CS2_KEY_VECTOR:
                if (kv->GetSubType() != KV3_SUBTYPE_VECTOR ||
                    value->GetVector(key) != Vector(e.vector_value[0], e.vector_value[1], e.vector_value[2]))
                    return 8;

                break;

            case KEELS2_CS2_KEY_ANGLES:
                if (kv->GetSubType() != KV3_SUBTYPE_QANGLE ||
                    value->GetQAngle(key) != QAngle(e.vector_value[0], e.vector_value[1], e.vector_value[2]))
                    return 9;

                break;

            case KEELS2_CS2_KEY_COLOR:
                if (kv->GetSubType() != KV3_SUBTYPE_COLOR32 ||
                    value->GetColor(key) !=
                        Color(e.color_value[0], e.color_value[1], e.color_value[2], e.color_value[3]))
                    return 10;

                break;

            default: return 11;
        }
    }

    return 0;
}

extern "C" KV_INSPECT_EXPORT void KeelFixtureKeyValuesRetain(void* pointer)
{
    static_cast<CEntityKeyValues*>(pointer)->AddRef();
}

extern "C" KV_INSPECT_EXPORT void KeelFixtureKeyValuesRelease(void* pointer)
{
    static_cast<CEntityKeyValues*>(pointer)->Release();
}
