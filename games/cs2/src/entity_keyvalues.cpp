#include <keels2/cs2/entity_keyvalues.h>
#include <entity2/entitykeyvalues.h>
#include <array>
#include <cmath>
#include <cstdint>

namespace {
std::size_t TextLength(const char* value, std::size_t maximum, bool identifier)
{
    if (!value)
        return maximum + 1;

    std::size_t length{};

    while (length <= maximum && value[length]) {
        const auto c = static_cast<unsigned char>(value[length]);

        if (identifier && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '_') return maximum+1;

        ++length;
    }

    return length;
}
}

extern "C" KeelResult KeelCs2KeyValues_Build(const char* class_name,
    const KeelCs2EntityKeyValue* values, std::uint32_t count, void** output)
{
    if (output)
        *output = nullptr;

    if (!output || count > KEELS2_CS2_KEY_MAX_COUNT || (count && !values))
        return KEEL_RESULT_INVALID_ARGUMENT;

    const auto class_length = TextLength(class_name,KEELS2_CS2_KEY_MAX_NAME,true);

    if (!class_length || class_length > KEELS2_CS2_KEY_MAX_NAME)
        return KEEL_RESULT_INVALID_ARGUMENT;

    std::size_t bytes = class_length+1;
    std::array<KeelCs2EntityKeyValue,KEELS2_CS2_KEY_MAX_COUNT> copy{};
    std::array<std::uint32_t,KEELS2_CS2_KEY_MAX_COUNT> hashes{};

    for (std::uint32_t i = 0; i < count; ++i) {
        copy[i] = values[i];
        const auto& value = copy[i];

        if (value.size != sizeof(value))
            return KEEL_RESULT_INVALID_ARGUMENT;

        const auto length = TextLength(value.name,KEELS2_CS2_KEY_MAX_NAME,false);

        if (!length || length > KEELS2_CS2_KEY_MAX_NAME)
            return KEEL_RESULT_INVALID_ARGUMENT;

        for (std::size_t j = 0; j < length; ++j) {
            const auto c = static_cast<unsigned char>(value.name[j]);

            if (c < 32 || c == 127)
                return KEEL_RESULT_INVALID_ARGUMENT;
        }

        hashes[i] = EntityKeyId_t(value.name).GetHashCode();

        if (hashes[i] == EntityKeyId_t("classname").GetHashCode())
            return KEEL_RESULT_INVALID_ARGUMENT;

        for (std::uint32_t j = 0; j < i; ++j)
            if (hashes[i] == hashes[j])
                return KEEL_RESULT_INVALID_ARGUMENT;

        bytes += length+1;

        if (value.type == KEELS2_CS2_KEY_STRING) {
            const auto string_length = TextLength(value.string_value,KEELS2_CS2_KEY_MAX_STRING,false);

            if (string_length > KEELS2_CS2_KEY_MAX_STRING)
                return KEEL_RESULT_INVALID_ARGUMENT;

            bytes += string_length+1;
        } else if (value.type == KEELS2_CS2_KEY_BOOL) {
            if (value.int_value != 0 && value.int_value != 1)
                return KEEL_RESULT_INVALID_ARGUMENT;
        } else if (value.type == KEELS2_CS2_KEY_FLOAT) {
            if (!std::isfinite(value.float_value))
                return KEEL_RESULT_INVALID_ARGUMENT;
        } else if (value.type == KEELS2_CS2_KEY_VECTOR || value.type == KEELS2_CS2_KEY_ANGLES) {
            for (float component : value.vector_value)
                if (!std::isfinite(component))
                    return KEEL_RESULT_INVALID_ARGUMENT;
        }
        else if (value.type != KEELS2_CS2_KEY_INT32 && value.type != KEELS2_CS2_KEY_COLOR)
            return KEEL_RESULT_INVALID_ARGUMENT;

        if (bytes > KEELS2_CS2_KEY_MAX_BYTES)
            return KEEL_RESULT_BUSY;
    }

    if (!g_pMemAlloc)
        return KEEL_RESULT_NOT_READY;

    CEntityKeyValues* result{};

    try {
        result = new CEntityKeyValues();
        result->AddRef();
        result->SetString("classname",class_name);

        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& value = copy[i];
            const EntityKeyId_t name(value.name);

            switch (value.type) {
            case KEELS2_CS2_KEY_STRING:
                result->SetString(name, value.string_value);
                break;

            case KEELS2_CS2_KEY_BOOL:
                result->SetBool(name, value.int_value != 0);
                break;

            case KEELS2_CS2_KEY_INT32:
                result->SetInt(name, value.int_value);
                break;

            case KEELS2_CS2_KEY_FLOAT:
                result->SetFloat(name, value.float_value);
                break;

            case KEELS2_CS2_KEY_VECTOR:
                result->SetVector(name, Vector(value.vector_value[0], value.vector_value[1], value.vector_value[2]));
                break;

            case KEELS2_CS2_KEY_ANGLES:
                result->SetQAngle(name, QAngle(value.vector_value[0], value.vector_value[1], value.vector_value[2]));
                break;

            case KEELS2_CS2_KEY_COLOR:
                result->SetColor(
                    name,
                    Color(value.color_value[0], value.color_value[1], value.color_value[2], value.color_value[3]));

                break;
            }
        }

        *output = result;
        return KEEL_RESULT_OK;
    } catch (...) {
        if (result)
            result->Release();

        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

extern "C" void KeelCs2KeyValues_Release(void* values)
{
    if (values)
        static_cast<CEntityKeyValues*>(values)->Release();
}
