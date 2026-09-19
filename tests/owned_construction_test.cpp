#include <keels2/cs2/owned_construction.h>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

extern "C" {
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesMemoryStart();
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesMemoryStop();
KEELS2_CS2_KEYVALUES_EXPORT std::size_t KeelFixtureKeyValuesMemoryCount();
KEELS2_CS2_KEYVALUES_EXPORT int KeelFixtureKeyValuesInspect(void*,const char*,const KeelCs2EntityKeyValue*,unsigned);
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesRetain(void*);
KEELS2_CS2_KEYVALUES_EXPORT void KeelFixtureKeyValuesRelease(void*);
}

namespace {
void Require(bool condition, int line)
{
    if (!condition)
    {
        std::cerr << "construction failure at " << line << '\n';
        std::exit(1);
    }
}
#define CHECK(x) Require((x),__LINE__)
using Identity = keels2::host::GameEntityIdentity;
using Store = keels2::cs2::OwnedConstructions;

KeelCs2EntityKeyValue Key(const char* name, unsigned type = KEELS2_CS2_KEY_INT32)
{
    KeelCs2EntityKeyValue value{};
    value.size = sizeof(value);
    value.name = name;
    value.type = type;
    return value;
}

class Backend final : public keels2::cs2::ConstructionBackend
{
public:
    struct Entity
    {
        Identity identity;
        bool pending{true};
    };
    std::map<unsigned,Entity> entities;
    std::function<void()> on_create, on_validate, on_spawn, on_cancel, on_teleport;
    KeelResult ready{KEEL_RESULT_OK}, create_result{KEEL_RESULT_OK}, spawn_result{KEEL_RESULT_OK};
    bool dispatch{true};
    unsigned next{1}, creates{}, spawns{}, cancels{}, teleports{};
    std::uint64_t epoch{1};
    std::vector<KeelCs2EntityKeyValue> expected;
    void* retained{};

    KeelResult Ready() override
    {
        return ready;
    }

    KeelResult Create(const char* name, Identity& output) override {
        CHECK(std::string(name) == "prop_dynamic");
        ++creates;

        if (create_result != KEEL_RESULT_OK)
            return create_result;

        output = {static_cast<int>(next), next, epoch};
        ++next;
        entities.emplace(output.source2_handle, Entity{output});

        if (on_create)
            on_create();

        return KEEL_RESULT_OK;
    }

    KeelResult Validate(const Identity& identity) override {
        if (on_validate)
            on_validate();

        const auto it = entities.find(identity.source2_handle);
        return identity.epoch == epoch && it != entities.end() && it->second.pending ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }

    KeelResult Spawn(const Identity& identity, const void* values, KeelBool& invoked) override {
        CHECK(Validate(identity) == KEEL_RESULT_OK);
        CHECK(KeelFixtureKeyValuesInspect(
                  const_cast<void*>(values), "prop_dynamic", expected.data(), static_cast<unsigned>(expected.size())) ==
              0);

        if (!dispatch)
            return spawn_result;

        invoked = KEEL_TRUE;
        ++spawns;
        entities.at(identity.source2_handle).pending = false;
        retained = const_cast<void*>(values);
        KeelFixtureKeyValuesRetain(retained);

        if (on_spawn)
            on_spawn();

        return spawn_result;
    }

    KeelResult Cancel(const Identity& identity) override {
        if (identity.epoch != epoch)
            return KEEL_RESULT_NOT_FOUND;

        ++cancels;
        entities.erase(identity.source2_handle);

        if (on_cancel)
            on_cancel();

        return KEEL_RESULT_OK;
    }

    KeelResult Teleport(const Identity&, const KeelEntityTeleport& request) override {
        CHECK(request.position[0] == 123);
        ++teleports;

        if (on_teleport)
            on_teleport();

        CHECK(request.position[0] == 123);
        return KEEL_RESULT_OK;
    }

    KeelResult Visit(const Identity& identity, const char* name, KeelEntityAccessCallback callback, void* data) override {
        CHECK(std::string(name) == "CBaseEntity");
        void* instance = &entities.at(identity.source2_handle);
        return callback(data,&instance,1);
    }

    void ReleaseRetained() {
        if (retained)
        {
            KeelFixtureKeyValuesRelease(retained);
            retained = nullptr;
        }
    }
};
}

int main()
{
    KeelFixtureKeyValuesMemoryStart();
    Backend backend;
    {
        Store store(backend);
        std::uint64_t token{};
        Identity identity{};
        const auto create = [&]
        {
            return store.Create("prop_dynamic", token, identity);
        };
        CHECK(create() == KEEL_RESULT_OK && token && identity.epoch == 1 && store.Count() == 1);
        Identity described{};
        CHECK(store.Describe(token, described) == KEEL_RESULT_OK &&
              described.source2_handle == identity.source2_handle);

        char name[] = "Model", text[] = "models/test.vmdl";
        auto model = Key(name, KEELS2_CS2_KEY_STRING);
        model.string_value = text;
        CHECK(store.Set(token, model) == KEEL_RESULT_OK);
        name[0] = 'X';
        text[0] = 'X';
        auto expected_model = Key("Model", KEELS2_CS2_KEY_STRING);
        expected_model.string_value = "models/test.vmdl";
        auto count = Key("spawnflags");
        count.int_value = 7;
        CHECK(store.Set(token,count) == KEEL_RESULT_OK);
        count.name = "SPAWNFLAGS";
        count.int_value = 12;
        CHECK(store.Set(token,count) == KEEL_RESULT_OK);
        backend.expected = {expected_model,count};
        auto bad = Key("classname");
        CHECK(store.Set(token, bad) == KEEL_RESULT_INVALID_ARGUMENT);
        bad = Key("field_5007");
        CHECK(store.Set(token, bad) == KEEL_RESULT_OK);
        backend.expected.push_back(bad);
        bad.name = "field_129636";
        CHECK(store.Set(token, bad) == KEEL_RESULT_INVALID_ARGUMENT);
        KeelBool invoked = KEEL_TRUE;
        backend.dispatch = false;
        backend.spawn_result = KEEL_RESULT_NOT_READY;
        CHECK(store.Spawn(token,invoked) == KEEL_RESULT_NOT_READY && !invoked && store.Count() == 1);
        backend.dispatch = true;
        backend.spawn_result = KEEL_RESULT_OK;
        backend.on_spawn = [&] {
            KeelBool nested = KEEL_TRUE;
            CHECK(store.Spawn(token,nested) == KEEL_RESULT_BUSY && !nested);
            CHECK(store.Set(token,count) == KEEL_RESULT_BUSY);
            CHECK(store.Cancel(token) == KEEL_RESULT_OK);
            CHECK(store.Set(token,count) == KEEL_RESULT_NOT_FOUND);
            CHECK(backend.cancels == 0);
        };
        CHECK(store.Spawn(token,invoked) == KEEL_RESULT_OK && invoked && !store.Count() && backend.cancels == 0);
        CHECK(store.Cancel(token) == KEEL_RESULT_NOT_FOUND);
        CHECK(KeelFixtureKeyValuesInspect(backend.retained,
                                          "prop_dynamic",
                                          backend.expected.data(),
                                          static_cast<unsigned>(backend.expected.size())) == 0);

        backend.ReleaseRetained();
        CHECK(KeelFixtureKeyValuesMemoryCount() == 0);
        backend.on_spawn = {};
        backend.expected.clear();
        CHECK(create() == KEEL_RESULT_OK);
        backend.on_spawn = [&]
        {
            store.Reset();
            throw std::runtime_error("spawn callback");
        };
        CHECK(store.Spawn(token,invoked) == KEEL_RESULT_ENGINE_FAILURE && invoked && !store.Count() && backend.cancels == 0);
        backend.ReleaseRetained();
        backend.on_spawn = {};
        CHECK(create() == KEEL_RESULT_OK);
        KeelEntityTeleport request{sizeof(request), KEELS2_TELEPORT_POSITION, {123, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        backend.on_teleport = [&]
        {
            request.position[0] = 999;
            CHECK(store.Cancel(token) == KEEL_RESULT_OK && backend.cancels == 0);
        };
        CHECK(store.Teleport(token,request) == KEEL_RESULT_NOT_FOUND && backend.cancels == 1 && !store.Count());
        backend.on_teleport = {};
        CHECK(create() == KEEL_RESULT_OK);
        backend.on_cancel = [&]
        {
            CHECK(store.Cancel(token) == KEEL_RESULT_NOT_FOUND);
            throw std::runtime_error("cancel callback");
        };
        CHECK(store.Cancel(token) == KEEL_RESULT_ENGINE_FAILURE && backend.cancels == 2 && !store.Count());
        backend.on_cancel = {};
        const auto before = backend.cancels;
        backend.on_create = [&]
        {
            store.Reset();
        };
        CHECK(create() == KEEL_RESULT_NOT_FOUND && !token && !identity.epoch && backend.cancels == before+1 && !store.Count());
        backend.on_create = {};
        backend.on_validate = []
        {
            throw std::runtime_error("validation");
        };
        CHECK(create() == KEEL_RESULT_ENGINE_FAILURE && backend.cancels == before+2 && !store.Count());
        backend.on_validate = {};
        backend.create_result = KEEL_RESULT_UNSUPPORTED;
        CHECK(create() == KEEL_RESULT_UNSUPPORTED && !store.Count());
        backend.create_result = KEEL_RESULT_OK;
        CHECK(create() == KEEL_RESULT_OK);
        backend.ready = KEEL_RESULT_WRONG_THREAD;
        CHECK(store.Cancel(token) == KEEL_RESULT_WRONG_THREAD && store.Count() == 1);
        CHECK(store.Spawn(token,invoked) == KEEL_RESULT_WRONG_THREAD && !invoked);
        backend.ready = KEEL_RESULT_OK;

        struct VisitData
        {
            Store* store;
            std::uint64_t token;
            unsigned calls{};
        } visit{&store, token};

        const auto callback = [](void* data, void* const* values, std::uint32_t n) -> KeelResult
        {
            auto& context = *static_cast<VisitData*>(data);
            CHECK(n == 1 && values[0]);
            ++context.calls;
            CHECK(context.store->Cancel(context.token) == KEEL_RESULT_OK);
            return KEEL_RESULT_UNSUPPORTED;
        };
        CHECK(store.Visit(token, "CBaseEntity", callback, &visit) == KEEL_RESULT_UNSUPPORTED && visit.calls == 1 &&
              !store.Count());

        CHECK(KeelFixtureKeyValuesMemoryCount() == 0);
        unsigned recursion{};
        backend.on_create = [&]
        {
            ++recursion;
            std::uint64_t nested{};
            Identity nested_identity{};
            CHECK(store.Create("prop_dynamic",nested,nested_identity) == (recursion == 8 ? KEEL_RESULT_BUSY : KEEL_RESULT_OK));
            --recursion;
        };
        CHECK(create() == KEEL_RESULT_OK && store.Count() == 8);
        backend.on_create = {};
        store.Reset();
        CHECK(create() == KEEL_RESULT_OK);
        char mutable_key[] = "origin", mutable_text[] = "1 2 3";
        auto copied = Key(mutable_key, KEELS2_CS2_KEY_STRING);
        copied.string_value = mutable_text;
        backend.on_validate = [&]
        {
            mutable_key[0] = 'X';
            mutable_text[0] = '9';
        };
        CHECK(store.Set(token, copied) == KEEL_RESULT_OK);
        backend.on_validate = {};
        auto expected_copy = Key("origin", KEELS2_CS2_KEY_STRING);
        expected_copy.string_value = "1 2 3";
        backend.expected = {expected_copy};
        CHECK(store.Spawn(token,invoked) == KEEL_RESULT_OK && invoked);
        backend.ReleaseRetained();
        backend.expected.clear();

        for (unsigned i = 0; i < Store::Capacity; ++i)
            CHECK(create() == KEEL_RESULT_OK);

        const auto creations = backend.creates;
        CHECK(create() == KEEL_RESULT_BUSY && !token && !identity.epoch && backend.creates == creations);
        backend.on_cancel = [&]
        {
            CHECK(create() == KEEL_RESULT_BUSY);
        };
        store.Reset();
        CHECK(!store.Count() && KeelFixtureKeyValuesMemoryCount() == 0);
        backend.on_cancel = {};
        CHECK(create() == KEEL_RESULT_OK);

        for (unsigned i = 0; i < KEELS2_CS2_KEY_MAX_COUNT; ++i) {
            const std::string key = "value_" + std::to_string(i);
            const auto item = Key(key.c_str());
            CHECK(store.Set(token,item) == KEEL_RESULT_OK);
        }

        CHECK(store.Set(token,Key("overflow")) == KEEL_RESULT_BUSY);
        CHECK(store.Set(token,Key("VALUE_1")) == KEEL_RESULT_OK);
        ++backend.epoch;
        backend.entities.clear();
        CHECK(store.Describe(token,described) == KEEL_RESULT_NOT_FOUND && !described.epoch);
        CHECK(store.Set(token,Key("value_1")) == KEEL_RESULT_NOT_FOUND);
        CHECK(store.Spawn(token,invoked) == KEEL_RESULT_NOT_FOUND && !invoked);
        CHECK(store.Cancel(token) == KEEL_RESULT_NOT_FOUND && !store.Count());
        CHECK(create() == KEEL_RESULT_OK);
        request.position[0] = 123;
        request.position[1] = std::numeric_limits<float>::quiet_NaN();
        CHECK(store.Teleport(token,request) == KEEL_RESULT_INVALID_ARGUMENT);
        request.flags = KEELS2_TELEPORT_ANGLES;
        request.angles[0] = std::numeric_limits<float>::infinity();
        CHECK(store.Teleport(token,request) == KEEL_RESULT_INVALID_ARGUMENT);
    }

    CHECK(KeelFixtureKeyValuesMemoryCount() == 0);
    KeelFixtureKeyValuesMemoryStop();
    return 0;
}
