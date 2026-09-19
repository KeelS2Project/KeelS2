#include <keels2/cs2/entity_variant.h>
#include <cstddef>
#include <variant.h>
#include <cmath>
#include <cstring>
#include <new>

static_assert(sizeof(CVariant) == 16 && alignof(CVariant) == 8);
static_assert(sizeof(fieldtype_t) == 1 && offsetof(CVariant,m_type) == 8 && offsetof(CVariant,m_flags) == 10);
static_assert(sizeof(Vector) == 12 && sizeof(QAngle) == 12 && sizeof(Color) == 4);

namespace {
void CopyPayload(CVariant& destination, fieldtype_t type, const void* source, std::size_t bytes)
{
    void* memory = MemAlloc_Alloc(bytes);
    if (!memory) throw std::bad_alloc();
    std::memcpy(memory,source,bytes);
    destination.m_pData = memory;
    destination.m_type = type;
    destination.m_flags = CV_FREE;
}
}

extern "C" KeelResult KeelCs2Variant_Build(const KeelCs2VariantValue* value, void** output)
{
    if (output) *output = nullptr;
    if (!output || !value || value->size != sizeof(*value)) return KEEL_RESULT_INVALID_ARGUMENT;
    const auto copy = *value;
    std::size_t string_length{};
    switch (copy.type) {
        case KEELS2_CS2_VARIANT_VOID:
        case KEELS2_CS2_VARIANT_INT32:
        case KEELS2_CS2_VARIANT_COLOR: break;
        case KEELS2_CS2_VARIANT_STRING:
            if (!copy.string_value) return KEEL_RESULT_INVALID_ARGUMENT;
            while (string_length <= KEELS2_CS2_VARIANT_MAX_STRING && copy.string_value[string_length]) ++string_length;
            if (string_length > KEELS2_CS2_VARIANT_MAX_STRING) return KEEL_RESULT_INVALID_ARGUMENT;
            break;
        case KEELS2_CS2_VARIANT_BOOL:
            if (copy.int_value != 0 && copy.int_value != 1) return KEEL_RESULT_INVALID_ARGUMENT;
            break;
        case KEELS2_CS2_VARIANT_FLOAT:
            if (!std::isfinite(copy.float_value)) return KEEL_RESULT_INVALID_ARGUMENT;
            break;
        case KEELS2_CS2_VARIANT_VECTOR:
        case KEELS2_CS2_VARIANT_ANGLES:
            for (float component : copy.vector_value) if (!std::isfinite(component)) return KEEL_RESULT_INVALID_ARGUMENT;
            break;
        case KEELS2_CS2_VARIANT_ENTITY:
            if (copy.entity_handle == INVALID_EHANDLE_INDEX) return KEEL_RESULT_INVALID_ARGUMENT;
            break;
        default: return KEEL_RESULT_INVALID_ARGUMENT;
    }
    if (!g_pMemAlloc) return KEEL_RESULT_NOT_READY;
    CVariant* result{};
    try {
        result = new CVariant();
        switch (copy.type) {
            case KEELS2_CS2_VARIANT_VOID: break;
            case KEELS2_CS2_VARIANT_STRING: CopyPayload(*result,FIELD_CSTRING,copy.string_value,string_length+1); break;
            case KEELS2_CS2_VARIANT_BOOL: *result = copy.int_value != 0; break;
            case KEELS2_CS2_VARIANT_INT32: *result = copy.int_value; break;
            case KEELS2_CS2_VARIANT_FLOAT: *result = copy.float_value; break;
            case KEELS2_CS2_VARIANT_VECTOR: {
                const Vector vector(copy.vector_value[0],copy.vector_value[1],copy.vector_value[2]);
                CopyPayload(*result,FIELD_VECTOR,&vector,sizeof(vector)); break;
            }
            case KEELS2_CS2_VARIANT_ANGLES: {
                const QAngle angles(copy.vector_value[0],copy.vector_value[1],copy.vector_value[2]);
                CopyPayload(*result,FIELD_QANGLE,&angles,sizeof(angles)); break;
            }
            case KEELS2_CS2_VARIANT_COLOR: {
                const Color color(copy.color_value[0],copy.color_value[1],copy.color_value[2],copy.color_value[3]);
                CopyPayload(*result,FIELD_COLOR32,&color,sizeof(color)); break;
            }
            case KEELS2_CS2_VARIANT_ENTITY: {
                *result = CEntityHandle(copy.entity_handle); break;
            }
        }
        *output = result;
        return KEEL_RESULT_OK;
    } catch (...) {
        delete result;
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}
extern "C" void KeelCs2Variant_Release(void* value) { delete static_cast<CVariant*>(value); }

extern "C" KeelResult KeelCs2Variant_Read(const void* pointer, KeelCs2VariantSnapshot* output)
{
    if (!output || output->size != sizeof(*output)) return KEEL_RESULT_INVALID_ARGUMENT;
    *output = {}; output->size = sizeof(*output); output->type = UINT32_MAX;
    if (!pointer) return KEEL_RESULT_INVALID_ARGUMENT;
    const auto& value = *static_cast<const CVariant*>(pointer);
    output->native_type = static_cast<uint32_t>(value.m_type);
    KeelCs2VariantSnapshot result{};
    result.size = sizeof(result); result.native_type = output->native_type;
    switch (value.m_type) {
        case FIELD_VOID: result.type = KEELS2_CS2_VARIANT_VOID; break;
        case FIELD_CSTRING:
        case FIELD_STRING: {
            result.type = KEELS2_CS2_VARIANT_STRING;
            const char* text = value.m_type == FIELD_CSTRING ? value.m_pszString : value.m_stringt.ToCStr();
            if (text) {
                std::size_t length{};
                while (length <= KEELS2_CS2_VARIANT_MAX_STRING && text[length]) ++length;
                if (length > KEELS2_CS2_VARIANT_MAX_STRING) return KEEL_RESULT_INCOMPATIBLE;
                std::memcpy(result.string_value,text,length);
            }
            break;
        }
        case FIELD_BOOLEAN: {
            result.type = KEELS2_CS2_VARIANT_BOOL;
            unsigned char boolean{};
            std::memcpy(&boolean,&value.m_bool,sizeof(boolean));
            if (boolean > 1) return KEEL_RESULT_INCOMPATIBLE;
            result.int_value = boolean; break;
        }
        case FIELD_INT32: result.type = KEELS2_CS2_VARIANT_INT32; result.int_value = value.m_int32; break;
        case FIELD_FLOAT32:
            result.type = KEELS2_CS2_VARIANT_FLOAT; result.float_value = value.m_float32;
            if (!std::isfinite(result.float_value)) return KEEL_RESULT_INCOMPATIBLE;
            break;
        case FIELD_VECTOR:
        case FIELD_QANGLE:
            result.type = value.m_type == FIELD_VECTOR ? KEELS2_CS2_VARIANT_VECTOR : KEELS2_CS2_VARIANT_ANGLES;
            if (!value.m_pData) return KEEL_RESULT_INCOMPATIBLE;
            std::memcpy(result.vector_value,value.m_pData,sizeof(result.vector_value));
            for (float component : result.vector_value) if (!std::isfinite(component)) return KEEL_RESULT_INCOMPATIBLE;
            break;
        case FIELD_COLOR32:
            result.type = KEELS2_CS2_VARIANT_COLOR;
            if (!value.m_pColor) return KEEL_RESULT_INCOMPATIBLE;
            for (int i = 0; i < 4; ++i) result.color_value[i] = (*value.m_pColor)[i];
            break;
        case FIELD_EHANDLE:
            result.type = KEELS2_CS2_VARIANT_ENTITY;
            result.entity_handle = static_cast<uint32_t>(value.m_hEntity.ToInt()); break;
        default: return KEEL_RESULT_UNSUPPORTED;
    }
    *output = result;
    return KEEL_RESULT_OK;
}
