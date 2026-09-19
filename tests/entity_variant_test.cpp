#include <keels2/cs2/entity_variant.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
extern "C" {
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesMemoryStart();
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesMemoryStop();
KEELS2_CS2_KEYVALUES_EXPORT std::size_t KeelFixtureKeyValuesMemoryCount();
KEELS2_CS2_KEYVALUES_EXPORT bool KeelFixtureKeyValuesMemoryOwns(void*);
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesMemoryFailAfter(int,bool);
KEELS2_CS2_KEYVALUES_EXPORT int KeelFixtureVariantInspect(void*,const KeelCs2VariantValue*);
KEELS2_CS2_KEYVALUES_EXPORT void* KeelFixtureVariantCopy(void*);
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureVariantRelease(void*);
}
namespace {
void Require(bool condition, int line) { if (!condition) { std::cerr << "variant failure at " << line << '\n'; std::exit(1); } }
#define CHECK(x) Require((x),__LINE__)
KeelCs2VariantValue Value(unsigned type) {
    KeelCs2VariantValue result{}; result.size = sizeof(result); result.type = type; return result;
}
}
int main()
{
    auto value = Value(0);
    void* native = reinterpret_cast<void*>(1);
    CHECK(KeelCs2Variant_Build(&value,&native) == KEEL_RESULT_NOT_READY && !native);
    KeelCs2Variant_Release(nullptr);
    KeelFixtureKeyValuesMemoryStart();
    auto* host_memory = new char[64];
    CHECK(!KeelFixtureKeyValuesMemoryOwns(host_memory)); delete[] host_memory;
    for (unsigned type = 0; type <= 8; ++type) {
        char text[] = "typed input \xC3\xA9";
        value = Value(type); value.string_value = text; value.int_value = type == 2 ? 1 : -2147483647;
        value.float_value = -1.25f; value.vector_value[0] = 12.5f; value.vector_value[1] = -90; value.vector_value[2] = 0.125f;
        value.color_value[0] = 3; value.color_value[1] = 200; value.color_value[2] = 255; value.color_value[3] = 0;
        value.entity_handle = 0x12340007;
        auto expected = value; expected.string_value = "typed input \xC3\xA9";
        CHECK(KeelCs2Variant_Build(&value,&native) == KEEL_RESULT_OK && native);
        CHECK(KeelFixtureKeyValuesMemoryCount() == (type == 1 || (type >= 5 && type <= 7) ? 2u : 1u));
        std::memset(text,'x',sizeof(text)-1); value.int_value = 99; value.vector_value[0] = 99; value.color_value[0] = 99;
        CHECK(KeelFixtureVariantInspect(native,&expected) == 0);
        void* retained = KeelFixtureVariantCopy(native);
        CHECK(retained != native && KeelFixtureVariantInspect(retained,&expected) == 0);
        KeelCs2Variant_Release(native);
        CHECK(KeelFixtureVariantInspect(retained,&expected) == 0);
        KeelFixtureVariantRelease(retained);
        CHECK(!KeelFixtureKeyValuesMemoryCount());
        // The engine-side consumer may destroy the actual produced object too.
        CHECK(KeelCs2Variant_Build(&expected,&native) == KEEL_RESULT_OK);
        KeelFixtureVariantRelease(native); CHECK(!KeelFixtureKeyValuesMemoryCount());
    }
    const auto reject = [&](KeelResult status = KEEL_RESULT_INVALID_ARGUMENT) {
        native = reinterpret_cast<void*>(1);
        CHECK(KeelCs2Variant_Build(&value,&native) == status && !native && !KeelFixtureKeyValuesMemoryCount());
    };
    CHECK(KeelCs2Variant_Build(nullptr,&native) == KEEL_RESULT_INVALID_ARGUMENT && !native);
    CHECK(KeelCs2Variant_Build(&value,nullptr) == KEEL_RESULT_INVALID_ARGUMENT);
    value = Value(0); --value.size; reject(); ++value.size;
    for (unsigned type : {9u,UINT32_MAX}) { value.type = type; reject(); }
    value = Value(1); reject();
    std::string long_text(4096,'s'); value.string_value = long_text.c_str(); reject();
    long_text.pop_back(); value.string_value = long_text.c_str();
    CHECK(KeelCs2Variant_Build(&value,&native) == KEEL_RESULT_OK && !KeelFixtureVariantInspect(native,&value));
    KeelCs2Variant_Release(native);
    value.string_value = "";
    CHECK(KeelCs2Variant_Build(&value,&native) == KEEL_RESULT_OK && !KeelFixtureVariantInspect(native,&value));
    KeelCs2Variant_Release(native);
    value = Value(2); value.int_value = -1; reject(); value.int_value = 2; reject(); value.int_value = 0;
    CHECK(KeelCs2Variant_Build(&value,&native) == KEEL_RESULT_OK && !KeelFixtureVariantInspect(native,&value));
    KeelCs2Variant_Release(native);
    for (float bad : {std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        value = Value(4); value.float_value = bad; reject();
        for (unsigned type : {5u,6u}) for (unsigned axis = 0; axis < 3; ++axis) { value = Value(type); value.vector_value[axis] = bad; reject(); }
    }
    value = Value(8); value.entity_handle = UINT32_MAX; reject(); value.entity_handle = 0;
    CHECK(KeelCs2Variant_Build(&value,&native) == KEEL_RESULT_OK && !KeelFixtureVariantInspect(native,&value));
    KeelCs2Variant_Release(native);
    for (unsigned type : {0u,3u,4u,8u}) {
        value = Value(type); value.string_value = reinterpret_cast<const char*>(1); value.int_value = INT32_MIN;
        CHECK(KeelCs2Variant_Build(&value,&native) == KEEL_RESULT_OK && !KeelFixtureVariantInspect(native,&value));
        KeelCs2Variant_Release(native);
    }
    for (bool throws : {false,true}) for (unsigned type : {1u,5u,6u,7u}) for (int after : {0,1}) {
        value = Value(type); value.string_value = "allocation failure";
        KeelFixtureKeyValuesMemoryFailAfter(after,throws); reject(KEEL_RESULT_ENGINE_FAILURE);
        KeelFixtureKeyValuesMemoryFailAfter(-1,false);
    }
    CHECK(!KeelFixtureKeyValuesMemoryCount()); KeelFixtureKeyValuesMemoryStop();
    return 0;
}
