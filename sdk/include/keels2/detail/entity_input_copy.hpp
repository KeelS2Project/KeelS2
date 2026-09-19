#ifndef KEELS2_DETAIL_ENTITY_INPUT_COPY_HPP
#define KEELS2_DETAIL_ENTITY_INPUT_COPY_HPP
#include <keels2/entity_input.h>
#include <array>
#include <cmath>
#include <cstring>
namespace keels2::detail {
struct EntityInputCopy
{
    std::array<char,KEELS2_INPUT_MAX_NAME+1> name{};
    std::array<char,KEELS2_INPUT_MAX_STRING+1> text{};
    KeelEntityInputValue value{};
    EntityInputCopy() = default;
    EntityInputCopy(const EntityInputCopy&) = delete;
    EntityInputCopy& operator=(const EntityInputCopy&) = delete;

    KeelResult Assign(const char* input, const KeelEntityInputValue& source, KeelBool queued, float delay)
    {
        if (!input || !*input || source.size != sizeof(source) || source.type > KEELS2_INPUT_ENTITY ||
            queued > KEEL_TRUE || !std::isfinite(delay) || delay < 0 || (!queued && delay != 0))
            return KEEL_RESULT_INVALID_ARGUMENT;

        std::size_t length{};

        while (length < name.size() && input[length]) {
            const auto c = static_cast<unsigned char>(input[length]);

            if (c < 32 || c == 127)
                return KEEL_RESULT_INVALID_ARGUMENT;

            name[length++] = static_cast<char>(c);
        }

        if (length == name.size())
            return KEEL_RESULT_INVALID_ARGUMENT;

        name[length] = 0;
        value = source;

        if (source.type == KEELS2_INPUT_STRING) {
            if (!source.string_value)
                return KEEL_RESULT_INVALID_ARGUMENT;

            length = 0;

            while (length < text.size() && source.string_value[length])
                ++length;

            if (length == text.size())
                return KEEL_RESULT_INVALID_ARGUMENT;

            std::memcpy(text.data(), source.string_value, length + 1);
            value.string_value = text.data();
        } else value.string_value = nullptr;

        if (source.type == KEELS2_INPUT_BOOL && source.int_value != 0 && source.int_value != 1)
            return KEEL_RESULT_INVALID_ARGUMENT;

        if (source.type == KEELS2_INPUT_FLOAT && !std::isfinite(source.float_value))
            return KEEL_RESULT_INVALID_ARGUMENT;

        if (source.type == KEELS2_INPUT_VECTOR || source.type == KEELS2_INPUT_ANGLES)
            for (float component : source.vector_value)
                if (!std::isfinite(component))
                    return KEEL_RESULT_INVALID_ARGUMENT;

        return KEEL_RESULT_OK;
    }
};
}
#endif
