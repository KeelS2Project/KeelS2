#include <keels2/cs2/entity_variant.h>
#include <cstddef>
#include <variant.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<KeelCs2VariantSnapshot>);
static_assert(sizeof(KeelCs2VariantSnapshot) == 4136 && alignof(KeelCs2VariantSnapshot) == 4);
#define CHECK(x) do { if (!(x)) { std::cerr << "snapshot failure at " << __LINE__ << '\n'; return 1; } } while (false)

int main()
{
    KeelCs2VariantSnapshot snapshot{};
    snapshot.size = sizeof(snapshot);

    const auto read = [&](const CVariant& value)
    {
        return KeelCs2Variant_Read(&value, &snapshot);
    };
    const auto cleared = [&] {
        KeelCs2VariantSnapshot expected{};
        expected.size = sizeof(expected);
        expected.type = UINT32_MAX;
        expected.native_type = snapshot.native_type;
        return !std::memcmp(&snapshot,&expected,sizeof(snapshot));
    };
    CHECK(KeelCs2Variant_Read(nullptr,&snapshot) == KEEL_RESULT_INVALID_ARGUMENT && cleared());
    CVariant nothing;
    CHECK(KeelCs2Variant_Read(&nothing,nullptr) == KEEL_RESULT_INVALID_ARGUMENT);
    --snapshot.size;
    CHECK(KeelCs2Variant_Read(&nothing,&snapshot) == KEEL_RESULT_INVALID_ARGUMENT && snapshot.size == sizeof(snapshot)-1);
    ++snapshot.size;
    CHECK(read(nothing) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_VOID);

    for (bool flag : {false,true}) {
        CVariant value(flag);
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_BOOL &&
              snapshot.int_value == static_cast<int32_t>(flag));
    }

    for (int integer : {0,INT32_MIN,INT32_MAX}) {
        CVariant value(integer);
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_INT32 && snapshot.int_value == integer);
    }

    for (float scalar : {-0.0f,0.125f,-4096.5f}) {
        CVariant value(scalar);
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_FLOAT && snapshot.float_value == scalar);
    }

    for (uint32_t handle : {0u,0x12340007u,UINT32_MAX}) {
        CVariant value{CEntityHandle(handle)};
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_ENTITY && snapshot.entity_handle == handle);
    }

    char text[] = "output \xC3\xA9";
    {
        CVariant value(text,false);
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_STRING &&
              !std::strcmp(snapshot.string_value, text));

        std::memset(text,'x',sizeof(text)-1);
    }

    CHECK(!std::strcmp(snapshot.string_value,"output \xC3\xA9"));
    const auto retained = snapshot;

    for (const char* string : {static_cast<const char*>(nullptr),"","pooled output"}) {
        CVariant value(string,false);
        CHECK(read(value) == KEEL_RESULT_OK && !std::strcmp(snapshot.string_value,string ? string : ""));
        value = static_cast<string_t>(castable_string_t(string));
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.native_type == FIELD_STRING &&
              !std::strcmp(snapshot.string_value, string ? string : ""));
    }

    CHECK(!std::strcmp(retained.string_value,"output \xC3\xA9"));
    char bounded[KEELS2_CS2_VARIANT_MAX_STRING + 2];
    std::memset(bounded, 's', sizeof(bounded));
    bounded[KEELS2_CS2_VARIANT_MAX_STRING] = 0;
    CVariant string(bounded,false);
    CHECK(read(string) == KEEL_RESULT_OK && std::strlen(snapshot.string_value) == KEELS2_CS2_VARIANT_MAX_STRING);
    bounded[KEELS2_CS2_VARIANT_MAX_STRING] = 's';
    bounded[KEELS2_CS2_VARIANT_MAX_STRING + 1] = 0;

    CHECK(read(string) == KEEL_RESULT_INCOMPATIBLE && cleared());
    {
        Vector vector(1.25f, -8, 90);
        CVariant value;
        value = &vector;
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_VECTOR);
        vector.Init(99,99,99);
    }

    CHECK(snapshot.vector_value[0] == 1.25f && snapshot.vector_value[1] == -8 && snapshot.vector_value[2] == 90);
    {
        QAngle angles(-90, 45.5f, 180);
        CVariant value;
        value = &angles;
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_ANGLES);
        angles.Init(99,99,99);
    }

    CHECK(snapshot.vector_value[0] == -90 && snapshot.vector_value[1] == 45.5f && snapshot.vector_value[2] == 180);
    {
        Color color(3, 200, 255, 0);
        CVariant value;
        value = &color;
        CHECK(read(value) == KEEL_RESULT_OK && snapshot.type == KEELS2_CS2_VARIANT_COLOR);
        color.SetColor(9,9,9,9);
    }

    CHECK(snapshot.color_value[0] == 3 && snapshot.color_value[1] == 200 && snapshot.color_value[2] == 255 &&
          !snapshot.color_value[3]);

    for (float invalid : {std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()})
    {
        CVariant value(invalid);
        CHECK(read(value) == KEEL_RESULT_INCOMPATIBLE && cleared());

        for (int axis = 0; axis < 3; ++axis) {
            Vector vector(1, 2, 3);
            vector[axis] = invalid;
            CVariant v;
            v = &vector;
            CHECK(read(v) == KEEL_RESULT_INCOMPATIBLE && cleared());
            QAngle angles(1, 2, 3);
            angles[axis] = invalid;
            CVariant a;
            a = &angles;
            CHECK(read(a) == KEEL_RESULT_INCOMPATIBLE && cleared());
        }
    }

    CVariant malformed;
    malformed.m_type = FIELD_BOOLEAN;
    const unsigned char bad_boolean = 2;
    std::memcpy(&malformed.m_bool,&bad_boolean,1);
    CHECK(read(malformed) == KEEL_RESULT_INCOMPATIBLE && cleared());
    malformed.m_pData = nullptr;

    for (fieldtype_t type : {FIELD_VECTOR,FIELD_QANGLE,FIELD_COLOR32}) {
        malformed.m_type = type;
        CHECK(read(malformed) == KEEL_RESULT_INCOMPATIBLE && cleared());
    }

    malformed.m_pData = reinterpret_cast<void*>(1);

    for (unsigned type = 0; type <= 255; ++type) {
        if (type == FIELD_VOID || type == FIELD_CSTRING || type == FIELD_STRING || type == FIELD_INT32 ||
            type == FIELD_BOOLEAN || type == FIELD_FLOAT32 || type == FIELD_VECTOR || type == FIELD_QANGLE ||
            type == FIELD_COLOR32 || type == FIELD_EHANDLE) continue;

        malformed.m_type = static_cast<fieldtype_t>(type);
        CHECK(read(malformed) == KEEL_RESULT_UNSUPPORTED && snapshot.native_type == type && cleared());
    }

    return 0;
}
