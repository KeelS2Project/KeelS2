#include <keels2/cs2/entity_variant.h>
#include <cstddef>
#include <variant.h>
#include <cstring>
#if defined(_WIN32)
#define VARIANT_INSPECT_EXPORT __declspec(dllexport)
#else
#define VARIANT_INSPECT_EXPORT __attribute__((visibility("default")))
#endif
extern "C" KEELS2_CS2_KEYVALUES_EXPORT bool KeelFixtureKeyValuesMemoryOwns(void*);
static_assert(sizeof(CVariant) == 16 && alignof(CVariant) == 8);
static_assert(offsetof(CVariant,m_type) == 8 && offsetof(CVariant,m_flags) == 10);
extern "C" VARIANT_INSPECT_EXPORT int KeelFixtureVariantInspect(void* pointer, const KeelCs2VariantValue* expected)
{
    if (!pointer || !expected || !KeelFixtureKeyValuesMemoryOwns(pointer)) return 1;
    const auto& value = *static_cast<CVariant*>(pointer);
    const auto& e = *expected;
    const bool owned = e.type == 1 || e.type == 5 || e.type == 6 || e.type == 7;
    if (value.m_flags != (owned ? CV_FREE : 0) || (owned && !KeelFixtureKeyValuesMemoryOwns(value.m_pData))) return 2;
    switch (e.type) {
        case KEELS2_CS2_VARIANT_VOID: return value.m_type == FIELD_VOID && value.m_pData == nullptr ? 0 : 3;
        case KEELS2_CS2_VARIANT_STRING: return value.m_type == FIELD_CSTRING && !std::strcmp(value.m_pszString,e.string_value) ? 0 : 4;
        case KEELS2_CS2_VARIANT_BOOL: return value.m_type == FIELD_BOOLEAN && value.m_bool == (e.int_value != 0) ? 0 : 5;
        case KEELS2_CS2_VARIANT_INT32: return value.m_type == FIELD_INT32 && value.m_int32 == e.int_value ? 0 : 6;
        case KEELS2_CS2_VARIANT_FLOAT: return value.m_type == FIELD_FLOAT32 && value.m_float32 == e.float_value ? 0 : 7;
        case KEELS2_CS2_VARIANT_VECTOR: return value.m_type == FIELD_VECTOR && *value.m_pVector == Vector(e.vector_value[0],e.vector_value[1],e.vector_value[2]) ? 0 : 8;
        case KEELS2_CS2_VARIANT_ANGLES: return value.m_type == FIELD_QANGLE && *value.m_pQAngle == QAngle(e.vector_value[0],e.vector_value[1],e.vector_value[2]) ? 0 : 9;
        case KEELS2_CS2_VARIANT_COLOR: return value.m_type == FIELD_COLOR32 && *value.m_pColor == Color(e.color_value[0],e.color_value[1],e.color_value[2],e.color_value[3]) ? 0 : 10;
        case KEELS2_CS2_VARIANT_ENTITY: return value.m_type == FIELD_EHANDLE && static_cast<uint32_t>(value.m_hEntity.ToInt()) == e.entity_handle ? 0 : 11;
        default: return 12;
    }
}
extern "C" VARIANT_INSPECT_EXPORT void* KeelFixtureVariantCopy(void* pointer)
{
    auto* value = new CVariant();
    static_cast<CVariant*>(pointer)->AssignTo(value);
    return value;
}
extern "C" VARIANT_INSPECT_EXPORT void KeelFixtureVariantRelease(void* pointer) { delete static_cast<CVariant*>(pointer); }
