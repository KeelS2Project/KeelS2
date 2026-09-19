#include "host.h"
#include "keelhook_service.h"
#include <keels2/keelhook.hpp>
#include <keels2/platform/loaded_module.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
extern "C" __declspec(dllimport) std::int32_t KeelHookBackendTarget(std::int32_t);
#else
extern "C" std::int32_t KeelHookBackendTarget(std::int32_t);
#endif
namespace keels2::host {
namespace {
void Check(bool value, int line)
{
    if (!value)
        throw std::runtime_error("completion check " + std::to_string(line));
}
#define CHECK(value) Check((value),__LINE__)
struct Probe {
    KeelHookService* service{};
    KeelHookApi api{};
    KeelHookCallbackHandle callback{};
    unsigned mode{}, depth{}, pre{}, post{}, completed{};
    bool failed{};

    struct Cleanup
    {
        Probe* probe;
        unsigned index;
    };
    std::array<Cleanup,9> cleanups{};
    std::vector<unsigned> order;

    void Require(bool value)
    {
        failed |= !value;
    }

    static void Complete(void* data) {
        auto& entry = *static_cast<Cleanup*>(data);
        auto& probe = *entry.probe;
        const auto snapshots = probe.service->Snapshots();
        probe.Require(snapshots.size() == 1 && snapshots[0].active > 0);
        probe.Require(KeelHookService::DeferInternal(nullptr,&Complete,data) == KEEL_RESULT_NOT_READY);
        ++probe.completed;
        probe.order.push_back(entry.index);

        if (probe.mode == 5 && entry.index == 4)
            throw std::runtime_error("cleanup failure");
    }

    static KeelHookAction Callback(KeelHookFrame* frame, void* data) {
        auto& probe = *static_cast<Probe*>(data);

        if (frame->phase == KH_PHASE_POST) {
            ++probe.post;
            probe.Require(((frame->flags & KH_FRAME_ORIGINAL_CALLED) != 0) == (probe.mode != 1));
            probe.Require(probe.mode == 4 && !probe.depth ? probe.completed == 1 : probe.completed == 0);
            return KH_ACTION_CONTINUE;
        }

        ++probe.pre;
        probe.Require(KeelHookService::DeferInternal(frame,nullptr,nullptr) == KEEL_RESULT_INVALID_ARGUMENT);
        const unsigned count = probe.mode == 5 ? 8 : 1;

        for (unsigned i = 0; i < count; ++i) {
            auto* entry = &probe.cleanups[i+probe.depth];
            probe.Require(KeelHookService::DeferInternal(frame,&Complete,entry) == KEEL_RESULT_OK);
            probe.Require(KeelHookService::DeferInternal(frame,&Complete,entry) == KEEL_RESULT_ALREADY_EXISTS);
        }

        if (probe.mode == 5)
            probe.Require(KeelHookService::DeferInternal(frame,&Complete,&probe.cleanups[8]) == KEEL_RESULT_BUSY);

        if (probe.mode == 1)
        {
            frame->result.scalar.int32 = 99;
            return KH_ACTION_SUPERSEDE;
        }

        if (probe.mode == 2)
            probe.Require(probe.api.set_callback_enabled(0,probe.callback,KEEL_FALSE) == KEEL_RESULT_OK);

        if (probe.mode == 3)
            throw std::runtime_error("pre failure");

        if (probe.mode == 4 && !probe.depth) {
            ++probe.depth;
            std::int32_t (*volatile call)(std::int32_t) = &KeelHookBackendTarget;
            probe.Require(call(2) == 13);
            --probe.depth;
            probe.Require(probe.completed == 1);
        }

        return KH_ACTION_CONTINUE;
    }

    static KeelHookAction Unprivileged(KeelHookFrame* frame, void* data) {
        auto& probe = *static_cast<Probe*>(data);
        probe.Require(KeelHookService::DeferInternal(frame,&Complete,&probe.cleanups[0]) == KEEL_RESULT_NOT_READY);
        return KH_ACTION_CONTINUE;
    }
};
}

struct SchemaEntityServiceTest {
    static void Run() {
        auto& host = Host::Instance();
        KeelHookService service(host);
        platform::LoadedModule module;
        std::string error;
        CHECK(platform::FindLoadedModuleForAddress(reinterpret_cast<void*>(&Probe::Callback), module, error) ==
              platform::ModuleLookup::found);

        service.Authorize(0, module.path, true);
        service.Authorize(1, module.path, true);
        Probe probe;
        probe.service = &service;
        probe.api = service.Api();

        for (unsigned i = 0; i < probe.cleanups.size(); ++i)
            probe.cleanups[i] = {&probe, i};

        const KeelHookTargetSpec spec{sizeof(spec),KH_TARGET_ADDRESS,KH_MECHANISM_DETOUR,0,
            nullptr,nullptr,nullptr,nullptr,reinterpret_cast<void*>(&KeelHookBackendTarget),0,0,0};

        constexpr auto prototype = kh::Prototype<std::int32_t(std::int32_t)>::value;
        KeelHookTargetHandle target{}, peer_target{};
        CHECK(probe.api.resolve_target(0,&spec,&prototype,&target) == KEEL_RESULT_OK);
        CHECK(probe.api.resolve_target(1,&spec,&prototype,&peer_target) == KEEL_RESULT_OK && peer_target == target);
        const KeelHookCallbackSpec peer_spec{sizeof(peer_spec), KH_PHASE_BOTH, 10, 0, &Probe::Unprivileged, &probe};
        KeelHookCallbackHandle peer{};
        CHECK(probe.api.add_callback(1,target,&peer_spec,&peer) == KEEL_RESULT_OK);
        const KeelHookCallbackSpec callback_spec{sizeof(callback_spec), KH_PHASE_BOTH, 0, 0, &Probe::Callback, &probe};
        CHECK(probe.api.add_callback(0,target,&callback_spec,&probe.callback) == KEEL_RESULT_OK);

        for (unsigned mode = 0; mode <= 5; ++mode) {
            probe.mode = mode;
            probe.pre = probe.post = probe.completed = 0;
            probe.order.clear();
            CHECK(probe.api.set_callback_enabled(0,probe.callback,KEEL_TRUE) == KEEL_RESULT_OK);
            std::int32_t (*volatile call)(std::int32_t) = &KeelHookBackendTarget;
            CHECK(call(2) == (mode == 1 ? 99 : 13));
            CHECK(!probe.failed);
            CHECK(probe.pre == (mode == 4 ? 2u : 1u));
            CHECK(probe.post == (mode == 2 ? 0u : mode == 4 ? 2u : 1u));
            CHECK(probe.completed == (mode == 5 ? 8u : mode == 4 ? 2u : 1u));

            if (mode == 4)
                CHECK(probe.order == std::vector<unsigned>({1, 0}));

            if (mode == 5)
                CHECK(probe.order == std::vector<unsigned>({7, 6, 5, 4, 3, 2, 1, 0}));

            CHECK(service.Snapshots()[0].active == 0);
        }

        CHECK(KeelHookService::DeferInternal(reinterpret_cast<KeelHookFrame*>(1), &Probe::Complete, nullptr) ==
              KEEL_RESULT_NOT_READY);

        CHECK(probe.api.remove_callback(0,probe.callback) == KEEL_RESULT_OK);
        CHECK(probe.api.remove_callback(1,peer) == KEEL_RESULT_OK);
        CHECK(probe.api.release_target(0,target) == KEEL_RESULT_OK);
        CHECK(probe.api.release_target(1,target) == KEEL_RESULT_OK);
        CHECK(service.Shutdown());
    }
};
}

int main() {
    try
    {
        keels2::host::SchemaEntityServiceTest::Run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
