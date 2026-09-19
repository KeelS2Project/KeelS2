#include <keels2/cs2/entity_keyvalues.h>
#include <array>
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
KEELS2_CS2_KEYVALUES_EXPORT int KeelFixtureKeyValuesInspect(void*,const char*,const KeelCs2EntityKeyValue*,unsigned);
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesRetain(void*);
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesRelease(void*);
}
namespace {
void Require(bool condition, int line) { if (!condition) { std::cerr << "keyvalues failure at " << line << '\n'; std::exit(1); } }
#define CHECK(x) Require((x),__LINE__)
KeelCs2EntityKeyValue Key(const char* name, unsigned type) {
    KeelCs2EntityKeyValue result{}; result.size = sizeof(result); result.type = type; result.name = name; return result;
}
}
int main()
{
    void* value = reinterpret_cast<void*>(1);
    CHECK(KeelCs2KeyValues_Build("prop_dynamic",nullptr,0,&value) == KEEL_RESULT_NOT_READY && !value);
    KeelCs2KeyValues_Release(nullptr);
    KeelFixtureKeyValuesMemoryStart();
    auto* host_memory = new char[100];
    CHECK(!KeelFixtureKeyValuesMemoryOwns(host_memory)); delete[] host_memory;
    char name[] = "model", text[] = "models/example.vmdl", class_name[] = "prop_dynamic";
    std::array<KeelCs2EntityKeyValue,7> entries{{Key(name,1),Key("enabled",2),Key("spawnflags",3),
        Key("scale",4),Key("origin",5),Key("angles",6),Key("rendercolor",7)}};
    entries[0].string_value = text; entries[1].int_value = 1; entries[2].int_value = -123;
    entries[3].float_value = 2.5f;
    for (unsigned i = 0; i < 3; ++i) { entries[4].vector_value[i] = static_cast<float>(i)+1; entries[5].vector_value[i] = static_cast<float>(i)*90; }
    entries[6].color_value[0] = 10; entries[6].color_value[1] = 100; entries[6].color_value[2] = 255; entries[6].color_value[3] = 200;
    auto expected = entries; expected[0].name = "model"; expected[0].string_value = "models/example.vmdl";
    CHECK(KeelCs2KeyValues_Build(class_name,entries.data(),static_cast<unsigned>(entries.size()),&value) == KEEL_RESULT_OK);
    CHECK(value && KeelFixtureKeyValuesMemoryOwns(value) && KeelFixtureKeyValuesMemoryCount() > 1);
    std::memset(name,'x',sizeof(name)-1); std::memset(text,'y',sizeof(text)-1); std::memset(class_name,'z',sizeof(class_name)-1);
    entries[2].int_value = 99; entries[4].vector_value[0] = 99;
    CHECK(KeelFixtureKeyValuesInspect(value,"prop_dynamic",expected.data(),static_cast<unsigned>(expected.size())) == 0);
    KeelFixtureKeyValuesRetain(value); KeelCs2KeyValues_Release(value);
    CHECK(KeelFixtureKeyValuesInspect(value,"prop_dynamic",expected.data(),static_cast<unsigned>(expected.size())) == 0);
    KeelFixtureKeyValuesRelease(value);
    CHECK(KeelFixtureKeyValuesMemoryCount() == 0);
    auto item = Key("key",1); item.string_value = "";
    const auto reject = [&](KeelResult status = KEEL_RESULT_INVALID_ARGUMENT, unsigned count = 1) {
        value = reinterpret_cast<void*>(1);
        CHECK(KeelCs2KeyValues_Build("prop_dynamic",&item,count,&value) == status && !value && !KeelFixtureKeyValuesMemoryCount());
    };
    for (const char* bad : {"","has space","bad;class"}) {
        value = reinterpret_cast<void*>(1);
        CHECK(KeelCs2KeyValues_Build(bad,nullptr,0,&value) == KEEL_RESULT_INVALID_ARGUMENT && !value);
    }
    CHECK(KeelCs2KeyValues_Build(nullptr,nullptr,0,&value) == KEEL_RESULT_INVALID_ARGUMENT && !value);
    CHECK(KeelCs2KeyValues_Build("prop_dynamic",nullptr,1,&value) == KEEL_RESULT_INVALID_ARGUMENT && !value);
    CHECK(KeelCs2KeyValues_Build("prop_dynamic",nullptr,0,nullptr) == KEEL_RESULT_INVALID_ARGUMENT);
    reject(KEEL_RESULT_INVALID_ARGUMENT,129);
    --item.size; reject(); ++item.size;
    for (unsigned type : {0u,8u,UINT32_MAX}) { item.type = type; reject(); }
    item.type = 1; item.string_value = nullptr; reject(); item.string_value = "";
    for (const char* bad : {"","ClassName","CLASSNAME","key\nname","key\x7f","key_1890dylc"}) { item.name = bad; reject(); }
    item.name = nullptr; reject(); item.name = "key";
    std::string long_name(128,'n'), long_text(4096,'s');
    item.name = long_name.c_str(); reject(); item.name = "key";
    item.string_value = long_text.c_str(); reject(); item.string_value = "";
    item.type = 2; item.int_value = -1; reject(); item.int_value = 2; reject(); item.int_value = 0;
    item.type = 4;
    for (float bad : {std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        item.float_value = bad; reject();
        for (unsigned type : {5u,6u}) for (unsigned axis = 0; axis < 3; ++axis) {
            item.type = type; item.vector_value[axis] = bad; reject(); item.vector_value[axis] = 0;
        }
        item.type = 4;
    }
    item.type = 3; item.int_value = std::numeric_limits<int>::min(); item.string_value = reinterpret_cast<const char*>(1);
    CHECK(KeelCs2KeyValues_Build("prop_dynamic",&item,1,&value) == KEEL_RESULT_OK);
    CHECK(KeelFixtureKeyValuesInspect(value,"prop_dynamic",&item,1) == 0); KeelCs2KeyValues_Release(value);
    auto duplicate = std::array{Key("Case.Name",3),Key("case.name",3)};
    CHECK(KeelCs2KeyValues_Build("prop_dynamic",duplicate.data(),2,&value) == KEEL_RESULT_INVALID_ARGUMENT && !value);
    duplicate = {Key("field_5007",3),Key("field_129636",3)};
    CHECK(KeelCs2KeyValues_Build("prop_dynamic",duplicate.data(),2,&value) == KEEL_RESULT_INVALID_ARGUMENT && !value);
    long_name.pop_back(); long_text.pop_back(); item = Key(long_name.c_str(),1); item.string_value = long_text.c_str();
    CHECK(KeelCs2KeyValues_Build(long_name.c_str(),&item,1,&value) == KEEL_RESULT_OK);
    CHECK(KeelFixtureKeyValuesInspect(value,long_name.c_str(),&item,1) == 0); KeelCs2KeyValues_Release(value);
    std::array<KeelCs2EntityKeyValue,128> many{}; std::array<std::string,128> names{};
    for (unsigned i = 0; i < many.size(); ++i) { names[i] = "value_"+std::to_string(i); many[i] = Key(names[i].c_str(),3); many[i].int_value = static_cast<int>(i); }
    for (unsigned iteration = 0; iteration < 20; ++iteration) {
        CHECK(KeelCs2KeyValues_Build("info_target",many.data(),128,&value) == KEEL_RESULT_OK);
        CHECK(KeelFixtureKeyValuesInspect(value,"info_target",many.data(),128) == 0); KeelCs2KeyValues_Release(value);
        CHECK(KeelFixtureKeyValuesMemoryCount() == 0);
    }
    for (unsigned i = 0; i < 8; ++i) { many[i].type = 1; many[i].string_value = long_text.c_str(); }
    std::array<std::string,7> texts{};
    for (unsigned i = 0; i < texts.size(); ++i) {
        texts[i] = std::string(4095,static_cast<char>('a'+i)); many[i].string_value = texts[i].c_str();
    }
    CHECK(KeelCs2KeyValues_Build("info_target",many.data(),7,&value) == KEEL_RESULT_OK);
    CHECK(KeelFixtureKeyValuesInspect(value,"info_target",many.data(),7) == 0); KeelCs2KeyValues_Release(value);
    CHECK(KeelCs2KeyValues_Build("info_target",many.data(),8,&value) == KEEL_RESULT_BUSY && !value && !KeelFixtureKeyValuesMemoryCount());
    many[0].string_value = ""; many[1].type = 2; many[1].int_value = 0;
    CHECK(KeelCs2KeyValues_Build("info_target",many.data(),2,&value) == KEEL_RESULT_OK);
    CHECK(KeelFixtureKeyValuesInspect(value,"info_target",many.data(),2) == 0); KeelCs2KeyValues_Release(value);
    CHECK(KeelCs2KeyValues_Build("info_target",nullptr,0,&value) == KEEL_RESULT_OK);
    CHECK(KeelFixtureKeyValuesInspect(value,"info_target",nullptr,0) == 0); KeelCs2KeyValues_Release(value);
    CHECK(KeelFixtureKeyValuesMemoryCount() == 0); KeelFixtureKeyValuesMemoryStop();
    return 0;
}
