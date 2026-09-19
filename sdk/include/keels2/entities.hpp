#ifndef KEELS2_ENTITIES_HPP
#define KEELS2_ENTITIES_HPP

#include <keels2/entities.h>
#include <keels2/entity_construction.h>
#include <keels2/detail/entity_input_copy.hpp>
#include <keels2/detail/authoring_status.hpp>
#include <keels2/player_actions.h>
#include <keels2/plugin.hpp>
#include <keels2/schema.hpp>
#include <Color.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace keels2::entities
{
class InputValue;

class Entity final
{
    struct Value
    {
        std::shared_ptr<keels2::detail::ContextState> context;
        const KeelEntitiesApi* api{};
        const KeelEntityConstructionApi* construction{};
        KeelEntityHandle handle{};
        KeelEntityInfo info{};
    };

    struct State
    {
        Value value;
        std::uint64_t generation{};
        bool alive{true};
        keels2::detail::AuthoringStatus status;
    };

    struct Snapshot
    {
        std::shared_ptr<State> state;
        Value value;
        std::uint64_t generation{};

        bool Current() const noexcept
        {
            return state && state->alive && state->generation == generation && value.handle &&
                value.context && value.context->accepting_resources.load(std::memory_order_acquire) && value.api;
        }
    };

public:
    Entity() = default;

    ~Entity()
    {
        if (state_)
        {
            state_->alive = false;
            static_cast<void>(Release(state_));
        }
    }

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    Entity(Entity&& other) noexcept : state_(std::move(other.state_))
    {
        empty_status_.Set(other.empty_status_.Result());
        other.empty_status_.Set(KEEL_RESULT_NOT_READY);
    }

    Entity& operator=(Entity&& other) noexcept
    {
        if (this != &other)
        {
            auto previous = std::move(state_);
            state_ = std::move(other.state_);
            empty_status_.Set(other.empty_status_.Result());
            other.empty_status_.Set(KEEL_RESULT_NOT_READY);

            if (previous)
            {
                previous->alive = false;
                static_cast<void>(Release(previous));
            }
        }

        return *this;
    }

    explicit operator bool() const noexcept
    {
        return Valid();
    }

    bool Valid() const noexcept
    {
        return Describe(Capture()) == KEEL_RESULT_OK;
    }

    // Pending entities accept copied keys, teleport and one invoked spawn.
    bool Pending() const noexcept
    {
        const auto held = Capture();
        auto& status = Status();
        bool pending{};
        const auto result = Describe(held, &pending);
        status.Set(result);
        return result == KEEL_RESULT_OK && pending;
    }

    // Pending cancellation requires the game thread. Spawned entities survive Reset.
    KeelResult Reset() noexcept
    {
        if (state_)
            return Release(state_);

        empty_status_.Set(KEEL_RESULT_NOT_READY);
        return KEEL_RESULT_OK;
    }

    int Index() const noexcept
    {
        const auto held = Capture();
        return held.Current() ? held.value.info.index : -1;
    }

    uint32 Source2Handle() const noexcept
    {
        const auto held = Capture();
        return held.Current() ? held.value.info.source2_handle : KEELS2_INVALID_SOURCE2_ENTITY_HANDLE;
    }

    template <typename Type>
    bool Read(const schema::Field<Type>& field, Type& value) const noexcept
    {
        static_assert(schema::detail::kSupportedValue<Type>);

        if constexpr (std::is_same_v<Type, Vector>) value.Init();
        else value = Type{};
        const auto held = Capture();
        auto& status = Status();
        const auto property = field.RawHandle();

        if (!held.Current() || !property)
            return status.Set(KEEL_RESULT_NOT_READY);

        if (field.context_ != held.value.context)
            return status.Set(KEEL_RESULT_INVALID_ARGUMENT);

        if (!held.value.api->read_field)
            return status.Set(KEEL_RESULT_INCOMPATIBLE);

        return status.Set(
            held.value.api->read_field(held.value.context->plugin, held.value.handle, property, &value, sizeof(value)));
    }

    KeelResult ApplyImpulse(const Vector& impulse, float damage = 0.0f) const noexcept
    {
        const KeelPlayerAction action{
            sizeof(action), KEELS2_PLAYER_ACTION_IMPULSE, {impulse.x, impulse.y, impulse.z}, damage};

        return ApplyAction(action);
    }

    KeelResult Kill() const noexcept
    {
        const KeelPlayerAction action{sizeof(action), KEELS2_PLAYER_ACTION_KILL, {}, 0.0f};
        return ApplyAction(action);
    }

    bool TryApplyImpulse(const Vector& impulse, float damage = 0.0f) const noexcept
    {
        return ApplyImpulse(impulse, damage) == KEEL_RESULT_OK;
    }

    bool TryKill() const noexcept
    {
        return Kill() == KEEL_RESULT_OK;
    }

    KeelResult LastResult() const noexcept
    {
        return Status().Result();
    }

    const char* LastError() const noexcept
    {
        return Status().Error();
    }

    bool Same(const Entity& other) const noexcept
    {
        const auto left = Capture(), right = other.Capture();

        if (!left.Current() || !right.Current() || left.value.context != right.value.context ||
            left.value.api != right.value.api)
            return false;

        bool left_pending{}, right_pending{};

        if (Describe(left, &left_pending) != KEEL_RESULT_OK || Describe(right, &right_pending) != KEEL_RESULT_OK ||
            !left.Current() || !right.Current())
            return false;

        if (left_pending || right_pending)
            return Describe(left) == KEEL_RESULT_OK && Identity(left.value.info, right.value.info);

        KeelBool equal{};
        return left.value.api->equal(left.value.context->plugin, left.value.handle, right.value.handle, &equal) ==
                   KEEL_RESULT_OK &&
               equal == KEEL_TRUE && left.Current() && right.Current() && Identity(left.value.info, right.value.info);
    }

    bool SetKey(const char* name, const char* text) const noexcept
    {
        KeelEntityKeyValue value{};
        value.size = sizeof(value);
        value.name = name;
        value.type = KEELS2_ENTITY_KEY_STRING;
        value.string_value = text;
        return SetKeyValue(value);
    }

    bool SetKey(const char* name, bool number) const noexcept
    {
        KeelEntityKeyValue value{};
        value.size = sizeof(value);
        value.name = name;
        value.type = KEELS2_ENTITY_KEY_BOOL;
        value.int_value = number ? 1 : 0;
        return SetKeyValue(value);
    }

    bool SetKey(const char* name, std::int32_t number) const noexcept
    {
        KeelEntityKeyValue value{};
        value.size = sizeof(value);
        value.name = name;
        value.type = KEELS2_ENTITY_KEY_INT32;
        value.int_value = number;
        return SetKeyValue(value);
    }

    bool SetKey(const char* name, float number) const noexcept
    {
        KeelEntityKeyValue value{};
        value.size = sizeof(value);
        value.name = name;
        value.type = KEELS2_ENTITY_KEY_FLOAT;
        value.float_value = number;
        return SetKeyValue(value);
    }

    bool SetKey(const char* name, const Vector& vector) const noexcept
    {
        KeelEntityKeyValue value{};
        value.size = sizeof(value);
        value.name = name;
        value.type = KEELS2_ENTITY_KEY_VECTOR;
        value.vector_value[0] = vector.x;
        value.vector_value[1] = vector.y;
        value.vector_value[2] = vector.z;
        return SetKeyValue(value);
    }

    bool SetKey(const char* name, const QAngle& angles) const noexcept
    {
        KeelEntityKeyValue value{};
        value.size = sizeof(value);
        value.name = name;
        value.type = KEELS2_ENTITY_KEY_ANGLES;
        value.vector_value[0] = angles.x;
        value.vector_value[1] = angles.y;
        value.vector_value[2] = angles.z;
        return SetKeyValue(value);
    }

    bool SetKey(const char* name, const Color& color) const noexcept
    {
        KeelEntityKeyValue value{};
        value.size = sizeof(value);
        value.name = name;
        value.type = KEELS2_ENTITY_KEY_COLOR;

        for (int i = 0; i < 4; ++i)
            value.color_value[i] = color[i];

        return SetKeyValue(value);
    }

    // An invoked spawn consumes construction even on failure; never retry it.
    bool DispatchSpawn(bool& invoked) const noexcept
    {
        invoked = false;
        const auto held = Capture();
        auto& status = Status();
        bool pending{};
        const auto ready = Describe(held, &pending);

        if (ready != KEEL_RESULT_OK)
            return status.Set(ready);

        if (!pending || !held.value.construction)
            return status.Set(KEEL_RESULT_NOT_READY);

        KeelBool called{};
        const auto result = held.value.construction->spawn(held.value.context->plugin, held.value.handle, &called);
        invoked = called != KEEL_FALSE;
        return status.Set(called <= KEEL_TRUE ? result : KEEL_RESULT_INCOMPATIBLE);
    }

    bool Teleport(const Vector* position = nullptr,
                  const QAngle* angles = nullptr,
                  const Vector* velocity = nullptr) const noexcept
    {
        KeelEntityTeleport request{};
        request.size = sizeof(request);

        if (position)
        {
            request.flags |= 1;
            request.position[0] = position->x;
            request.position[1] = position->y;
            request.position[2] = position->z;
        }

        if (angles)
        {
            request.flags |= 2;
            request.angles[0] = angles->x;
            request.angles[1] = angles->y;
            request.angles[2] = angles->z;
        }

        if (velocity)
        {
            request.flags |= 4;
            request.velocity[0] = velocity->x;
            request.velocity[1] = velocity->y;
            request.velocity[2] = velocity->z;
        }

        const auto held = Capture();
        auto& status = Status();

        if (!request.flags)
            return status.Set(KEEL_RESULT_INVALID_ARGUMENT);

        for (const auto* vector : {request.position, request.angles, request.velocity})
            for (unsigned i = 0; i < 3; ++i)
                if (!std::isfinite(vector[i]))
                    return status.Set(KEEL_RESULT_INVALID_ARGUMENT);

        bool pending{};
        const auto ready = Describe(held, &pending);

        if (ready != KEEL_RESULT_OK)
            return status.Set(ready);

        if (pending)
            return status.Set(
                held.value.construction->teleport(held.value.context->plugin, held.value.handle, &request));

        const KeelEntityToolsApi* tools{};
        const auto query = Tools(held, tools);
        return status.Set(
            query == KEEL_RESULT_OK ? tools->teleport(held.value.context->plugin, held.value.handle, &request) : query);
    }

    // Inputs require live entities. Invoked reports engine entry, including
    // failures; success does not establish that the input name exists.
    bool AcceptInput(const char* name, bool& invoked, const InputValue& value,
        const Entity* activator = nullptr, const Entity* caller = nullptr) const noexcept;

    bool AcceptInput(const char* name, bool& invoked,
        const Entity* activator = nullptr, const Entity* caller = nullptr) const noexcept;
    // The engine owns queued copies; Reset/plugin unload does not cancel them.
    // Delay must be finite/nonnegative. Current profiles refuse queued colors.
    bool QueueInput(const char* name, float delay, bool& invoked, const InputValue& value,
        const Entity* activator = nullptr, const Entity* caller = nullptr) const noexcept;

    bool QueueInput(const char* name, float delay, bool& invoked,
        const Entity* activator = nullptr, const Entity* caller = nullptr) const noexcept;

private:
    friend class Service;
    friend class InputValue;
    bool SendInput(const char*, bool&, const InputValue&, const Entity*, const Entity*, bool, float) const noexcept;

    static KeelResult InputApi(const std::shared_ptr<keels2::detail::ContextState>& context, KeelEntityInputApi& api)
    {
        api = {};

        if (!context || !context->accepting_resources.load(std::memory_order_acquire) || !context->api ||
            !context->api->query_service)
            return KEEL_RESULT_NOT_READY;

        const void* raw{};
        const auto result = context->api->query_service(
            context->plugin, KEELS2_ENTITY_INPUT_SERVICE_NAME, KEELS2_ENTITY_INPUT_API_VERSION, &raw);

        if (result != KEEL_RESULT_OK)
            return result;

        if (!context->accepting_resources.load(std::memory_order_acquire))
            return KEEL_RESULT_NOT_READY;

        const auto* table = static_cast<const KeelEntityInputApi*>(raw);

        if (!table || table->size != sizeof(*table) || table->api_version != KEELS2_ENTITY_INPUT_API_VERSION ||
            !table->capabilities || !table->dispatch) return KEEL_RESULT_INCOMPATIBLE;

        api = *table;
        return KEEL_RESULT_OK;
    }

    static bool Identity(const KeelEntityInfo& left, const KeelEntityInfo& right) noexcept
    {
        return right.size == sizeof(right) && !right.reserved && right.index == left.index &&
            right.source2_handle == left.source2_handle && right.epoch == left.epoch;
    }

    Snapshot Capture() const noexcept
    {
        const auto held = state_;
        return held ? Snapshot{held, held->value, held->generation} : Snapshot{};
    }

    keels2::detail::AuthoringStatus& Status() const noexcept
    {
        return state_ ? state_->status : empty_status_;
    }

    static KeelResult Release(std::shared_ptr<State> held) noexcept
    {
        const auto old = std::exchange(held->value, Value{});
        const auto generation = ++held->generation;
        held->status.Set(KEEL_RESULT_NOT_READY);

        if (!old.handle)
            return KEEL_RESULT_OK;

        if (!old.context || !old.context->accepting_resources.load(std::memory_order_acquire) || !old.api || !old.api->release)
            return KEEL_RESULT_NOT_READY;

        const auto result = old.api->release(old.context->plugin, old.handle);
        // These failures occur before the host releases ownership or calls the engine.
        if ((result == KEEL_RESULT_WRONG_THREAD || result == KEEL_RESULT_BUSY) && held->generation == generation && held->alive)
        {
            held->value = old;
            held->status.Set(result);
        }

        return result;
    }

    static KeelResult Describe(const Snapshot& held, bool* pending = nullptr) noexcept
    {
        if (pending)
            *pending = false;

        if (!held.Current())
            return KEEL_RESULT_NOT_READY;

        KeelEntityInfo info{};
        info.size = sizeof(info);
        const auto& value = held.value;
        auto result = value.construction ? value.construction->describe(value.context->plugin, value.handle, &info)
                                         : KEEL_RESULT_NOT_FOUND;

        if (value.construction && result == KEEL_RESULT_OK)
        {
            if (pending)
                *pending = true;
        }
        else if (result == KEEL_RESULT_NOT_FOUND) result = value.api->describe(value.context->plugin, value.handle, &info);

        if (result != KEEL_RESULT_OK)
            return result;

        return held.Current() && Identity(value.info, info) ? KEEL_RESULT_OK : KEEL_RESULT_NOT_FOUND;
    }

    static bool Text(const char* input, std::size_t maximum, bool empty, std::string& output)
    {
        if (!input)
            return false;

        std::size_t length{};

        while (length <= maximum && input[length])
            ++length;

        if (length > maximum || (!length && !empty))
            return false;

        output.assign(input, length);
        return true;
    }

    bool SetKeyValue(KeelEntityKeyValue value) const noexcept
    {
        const auto held = Capture();
        auto& status = Status();

        try
        {
            std::string name, text;

            if (!Text(value.name, KEELS2_ENTITY_KEY_MAX_NAME, false, name) ||
                (value.type == KEELS2_ENTITY_KEY_STRING && !Text(value.string_value, KEELS2_ENTITY_KEY_MAX_STRING, true, text)))
                return status.Set(KEEL_RESULT_INVALID_ARGUMENT);

            if (value.type == KEELS2_ENTITY_KEY_FLOAT && !std::isfinite(value.float_value))
                return status.Set(KEEL_RESULT_INVALID_ARGUMENT);

            if (value.type == KEELS2_ENTITY_KEY_VECTOR || value.type == KEELS2_ENTITY_KEY_ANGLES)
                for (const float number : value.vector_value)
                    if (!std::isfinite(number))
                        return status.Set(KEEL_RESULT_INVALID_ARGUMENT);

            value.name = name.c_str();
            value.string_value = text.c_str();
            bool pending{};
            const auto ready = Describe(held, &pending);

            if (ready != KEEL_RESULT_OK)
                return status.Set(ready);

            if (!pending || !held.value.construction)
                return status.Set(KEEL_RESULT_NOT_READY);

            return status.Set(held.value.construction->set(held.value.context->plugin, held.value.handle, &value));
        }
        catch (...)
        {
            return status.Set(KEEL_RESULT_ENGINE_FAILURE);
        }
    }

    static KeelResult Tools(const Snapshot& held, const KeelEntityToolsApi*& api) noexcept
    {
        const auto& context = held.value.context;

        if (!held.Current() || !context->api || !context->api->query_service)
            return KEEL_RESULT_NOT_READY;

        const void* raw{};
        const auto result = context->api->query_service(
            context->plugin, KEELS2_ENTITY_TOOLS_SERVICE_NAME, KEELS2_ENTITY_TOOLS_API_VERSION, &raw);

        if (result != KEEL_RESULT_OK)
            return result;

        api = static_cast<const KeelEntityToolsApi*>(raw);

        if (!api || api->size != sizeof(*api) || api->api_version != KEELS2_ENTITY_TOOLS_API_VERSION || !api->teleport)
            return KEEL_RESULT_INCOMPATIBLE;

        return held.Current() ? KEEL_RESULT_OK : KEEL_RESULT_NOT_READY;
    }

    KeelResult ApplyAction(const KeelPlayerAction& action) const noexcept
    {
        const auto held = Capture();
        auto& status = Status();

        const auto call = [&]() -> KeelResult
        {
            if (!held.Current() || !held.value.context->api || !held.value.context->api->query_service)
                return KEEL_RESULT_NOT_READY;

            const void* raw{};
            const auto result = held.value.context->api->query_service(held.value.context->plugin,
                KEELS2_PLAYER_ACTIONS_SERVICE_NAME, KEELS2_PLAYER_ACTIONS_API_VERSION, &raw);

            if (result != KEEL_RESULT_OK)
                return result;

            const auto* actions = static_cast<const KeelPlayerActionsApi*>(raw);

            if (!actions || actions->size != sizeof(*actions) ||
                actions->api_version != KEELS2_PLAYER_ACTIONS_API_VERSION || !actions->apply)
                return KEEL_RESULT_INCOMPATIBLE;

            if (!held.Current())
                return KEEL_RESULT_NOT_READY;

            return actions->apply(held.value.context->plugin, held.value.handle, &action);
        };
        const auto result = call();
        status.Set(result);
        return result;
    }

    std::shared_ptr<State> state_;
    mutable keels2::detail::AuthoringStatus empty_status_;
};

// Owns selected numeric/string data. Entity payloads retain the wrapper's
// identity/generation snapshot, so Reset/destruction makes them stale instead
// of retaining a dangling Entity pointer. Copies and moves are independent of
// the source value's storage. Strings are bounded to 4095 bytes; construction
// may allocate, as with std::string. Invalid text is refused when dispatched.
class InputValue final
{
public:
    InputValue() noexcept
    {
        value_.size = sizeof(value_);
        value_.type = KEELS2_INPUT_VOID;
    }

    explicit InputValue(const char* text) : InputValue()
    {
        value_.type = KEELS2_INPUT_STRING;
        valid_ = Entity::Text(text,KEELS2_INPUT_MAX_STRING,true,text_);
    }

    explicit InputValue(bool value) noexcept : InputValue()
    {
        value_.type = KEELS2_INPUT_BOOL;
        value_.int_value = value ? 1 : 0;
    }

    explicit InputValue(std::int32_t value) noexcept : InputValue()
    {
        value_.type = KEELS2_INPUT_INT32;
        value_.int_value = value;
    }

    explicit InputValue(float value) noexcept : InputValue()
    {
        value_.type = KEELS2_INPUT_FLOAT;
        value_.float_value = value;
    }

    explicit InputValue(const Vector& value) noexcept : InputValue()
    {
        value_.type = KEELS2_INPUT_VECTOR;
        value_.vector_value[0] = value.x;
        value_.vector_value[1] = value.y;
        value_.vector_value[2] = value.z;
    }

    explicit InputValue(const QAngle& value) noexcept : InputValue()
    {
        value_.type = KEELS2_INPUT_ANGLES;
        value_.vector_value[0] = value.x;
        value_.vector_value[1] = value.y;
        value_.vector_value[2] = value.z;
    }

    explicit InputValue(const Color& value) noexcept : InputValue()
    {
        value_.type = KEELS2_INPUT_COLOR;

        for (int i = 0; i < 4; ++i)
            value_.color_value[i] = value[i];
    }

    explicit InputValue(const Entity& entity) noexcept : InputValue()
    {
        value_.type = KEELS2_INPUT_ENTITY;
        entity_ = entity.Capture();
    }

private:
    friend class Entity;
    KeelEntityInputValue value_{};
    std::string text_;
    Entity::Snapshot entity_;
    bool valid_{true};
};
inline bool Entity::SendInput(const char* name, bool& invoked, const InputValue& input,
    const Entity* activator, const Entity* caller, bool queued, float delay) const noexcept
{
    invoked = false;
    const auto held = Capture();
    auto& status = Status();
    const auto finish = [&](KeelResult result) {
        // A callback may have installed a replacement entity in this wrapper.
        // Preserve the replacement's status and never touch the wrapper itself.
        if (!held.state || held.state->generation == held.generation)
            status.Set(result);

        return result == KEEL_RESULT_OK;
    };
    KeelBool called{};

    try
    {
        if (!held.Current())
            return finish(KEEL_RESULT_NOT_READY);

        if (!input.valid_)
            return finish(KEEL_RESULT_INVALID_ARGUMENT);

        const bool entity_value = input.value_.type == KEELS2_INPUT_ENTITY;
        const std::array<Snapshot, 4> participants{held,
                                                   activator ? activator->Capture() : Snapshot{},
                                                   caller ? caller->Capture() : Snapshot{},
                                                   entity_value ? input.entity_ : Snapshot{}};

        const bool supplied[]{true,activator != nullptr,caller != nullptr,entity_value};
        auto value = input.value_;
        value.string_value = value.type == KEELS2_INPUT_STRING ? input.text_.c_str() : nullptr;
        keels2::detail::EntityInputCopy copy;
        const auto copied = copy.Assign(name,value,queued ? KEEL_TRUE : KEEL_FALSE,delay);

        if (copied != KEEL_RESULT_OK)
            return finish(copied);
        // All user-owned wrapper/value storage has been captured. No source
        // Entity or InputValue is read after entering the first host callback.
        for (std::size_t i = 0; i < participants.size(); ++i) if (supplied[i]) {
                if (!participants[i].Current())
                    return finish(KEEL_RESULT_NOT_READY);

                if (participants[i].value.context != held.value.context)
                    return finish(KEEL_RESULT_INVALID_ARGUMENT);
        }

        for (std::size_t i = 0; i < participants.size(); ++i) if (supplied[i]) {
            bool pending{};
            const auto result = Describe(participants[i],&pending);

            if (result != KEEL_RESULT_OK)
                return finish(result);

            if (pending)
                return finish(KEEL_RESULT_NOT_READY);
        }

        KeelEntityInputApi api{};
        const auto query = InputApi(held.value.context,api);

        if (query != KEEL_RESULT_OK)
            return finish(query);

        for (std::size_t i = 0; i < participants.size(); ++i)
            if (supplied[i] && !participants[i].Current())
                return finish(KEEL_RESULT_NOT_READY);

        KeelEntityInputRequest request{};
        request.size = sizeof(request);
        request.input = copy.name.data();
        request.value = copy.value;
        request.activator = participants[1].value.handle;
        request.caller = participants[2].value.handle;
        request.value_entity = participants[3].value.handle;
        request.queued = queued ? KEEL_TRUE : KEEL_FALSE;
        request.delay = delay;
        const auto result = api.dispatch(held.value.context->plugin,held.value.handle,&request,&called);
        invoked = called != KEEL_FALSE;
        return finish(called <= KEEL_TRUE ? result : KEEL_RESULT_INCOMPATIBLE);
    }
    catch (...)
    {
        invoked = called != KEEL_FALSE;
        return finish(KEEL_RESULT_ENGINE_FAILURE);
    }
}

inline bool Entity::AcceptInput(const char* name, bool& invoked, const InputValue& value,
    const Entity* activator, const Entity* caller) const noexcept
{
    return SendInput(name,invoked,value,activator,caller,false,0);
}

inline bool
Entity::AcceptInput(const char* name, bool& invoked, const Entity* activator, const Entity* caller) const noexcept
{
    return SendInput(name, invoked, InputValue{}, activator, caller, false, 0);
}

inline bool Entity::QueueInput(const char* name, float delay, bool& invoked, const InputValue& value,
    const Entity* activator, const Entity* caller) const noexcept
{
    return SendInput(name,invoked,value,activator,caller,true,delay);
}

inline bool Entity::QueueInput(
    const char* name, float delay, bool& invoked, const Entity* activator, const Entity* caller) const noexcept
{
    return SendInput(name, invoked, InputValue{}, activator, caller, true, delay);
}

class Service final
{
public:
    KeelResult Connect(const Context& context) noexcept
    {
        api_ = nullptr;
        context_.reset();
        const void* raw{};
        const auto result = context.QueryService(KEELS2_ENTITIES_SERVICE_NAME, KEELS2_ENTITIES_API_VERSION, &raw);

        if (result != KEEL_RESULT_OK)
            return result;

        const auto* api = static_cast<const KeelEntitiesApi*>(raw);

        if (!api || api->size != sizeof(*api) || api->api_version != KEELS2_ENTITIES_API_VERSION ||
            !api->find_by_index || !api->find_by_source2_handle || !api->release || !api->describe || !api->equal ||
            !api->read_field)
            return KEEL_RESULT_INCOMPATIBLE;

        context_ = context.State();
        api_ = api;
        return KEEL_RESULT_OK;
    }

    explicit operator bool() const noexcept
    {
        return context_ && context_->accepting_resources.load(std::memory_order_acquire) && api_;
    }

    KeelResult Find(int index, Entity& output) const noexcept
    {
        const auto context = context_;
        const auto* api = api_;
        return Open(context, api, output, false, [=](KeelEntityHandle& handle, const KeelEntityConstructionApi*&) {
            return index < 0 ? KEEL_RESULT_INVALID_ARGUMENT : api->find_by_index(context->plugin, index, &handle);
        }, index, KEELS2_INVALID_SOURCE2_ENTITY_HANDLE);
    }

    KeelResult FindSource2(uint32 source, Entity& output) const noexcept
    {
        const auto context = context_;
        const auto* api = api_;
        return Open(
            context,
            api,
            output,
            false,
            [=](KeelEntityHandle& handle, const KeelEntityConstructionApi*&)
            {
                return source == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE
                           ? KEEL_RESULT_INVALID_ARGUMENT
                           : api->find_by_source2_handle(context->plugin, source, &handle);
            },
            -1,
            source);
    }

    KeelResult Create(const char* classname, Entity& output) const noexcept
    {
        const auto context = context_;
        const auto* api = api_;
        std::string name;

        try
        {
            if (!Entity::Text(classname, KEELS2_ENTITY_KEY_MAX_NAME, false, name))
                return KEEL_RESULT_INVALID_ARGUMENT;

            for (const auto c : name)
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                    return KEEL_RESULT_INVALID_ARGUMENT;
        }
        catch (...)
        {
            return KEEL_RESULT_ENGINE_FAILURE;
        }

        return Open(context, api, output, true, [&](KeelEntityHandle& handle, const KeelEntityConstructionApi*& construction) {
            const auto query = Construction(context, construction);
            return query == KEEL_RESULT_OK ? construction->create(context->plugin, name.c_str(), &handle) : query;
        });
    }

    KeelResult InputCapabilities(std::uint32_t& direct, std::uint32_t& queued) const noexcept
    {
        direct = queued = 0;
        const auto context = context_;

        try {
            if (!*this)
                return KEEL_RESULT_NOT_READY;

            KeelEntityInputApi api{};
            const auto query = Entity::InputApi(context,api);

            if (query != KEEL_RESULT_OK)
                return query;

            std::uint32_t a{}, b{};
            const auto result = api.capabilities(context->plugin,&a,&b);

            if (result != KEEL_RESULT_OK)
                return result;

            if (!context->accepting_resources.load(std::memory_order_acquire))
                return KEEL_RESULT_NOT_READY;

            if ((a | b) & ~((1u << (KEELS2_INPUT_ENTITY + 1)) - 1))
                return KEEL_RESULT_INCOMPATIBLE;

            direct = a;
            queued = b;
            return KEEL_RESULT_OK;
        }
        catch (...)
        {
            return KEEL_RESULT_ENGINE_FAILURE;
        }
    }

    KeelResult ConstructionReady() const noexcept
    {
        const auto context = context_;

        if (!*this)
            return KEEL_RESULT_NOT_READY;

        const KeelEntityConstructionApi* construction{};
        const auto query = Construction(context, construction);
        return query == KEEL_RESULT_OK ? construction->ready(context->plugin) : query;
    }

private:
    static KeelResult Construction(const std::shared_ptr<keels2::detail::ContextState>& context,
                                   const KeelEntityConstructionApi*& api) noexcept
    {
        if (!context || !context->accepting_resources.load(std::memory_order_acquire) || !context->api ||
            !context->api->query_service)
            return KEEL_RESULT_NOT_READY;

        const void* raw{};
        const auto result = context->api->query_service(
            context->plugin, KEELS2_ENTITY_CONSTRUCTION_SERVICE_NAME, KEELS2_ENTITY_CONSTRUCTION_API_VERSION, &raw);

        if (result != KEEL_RESULT_OK)
            return result;

        api = static_cast<const KeelEntityConstructionApi*>(raw);

        if (!api || api->size != sizeof(*api) || api->api_version != KEELS2_ENTITY_CONSTRUCTION_API_VERSION ||
            !api->ready || !api->create || !api->describe || !api->set || !api->teleport || !api->spawn ||
            !api->observe || !api->visit)
            return KEEL_RESULT_INCOMPATIBLE;

        return KEEL_RESULT_OK;
    }

    template <typename Factory>
    static KeelResult Open(const std::shared_ptr<keels2::detail::ContextState>& context,
                           const KeelEntitiesApi* api,
                           Entity& output,
                           bool created,
                           Factory factory,
                           int index = -1,
                           uint32 source = KEELS2_INVALID_SOURCE2_ENTITY_HANDLE) noexcept
    {
        std::shared_ptr<Entity::State> held;

        try
        {
            if (!output.state_)
                output.state_ = std::make_shared<Entity::State>();

            held = output.state_;
        }
        catch (...)
        {
            return KEEL_RESULT_ENGINE_FAILURE;
        }

        const auto generation = held->generation + 1;
        const auto released = Entity::Release(held);

        if (!held->alive || held->generation != generation)
            return KEEL_RESULT_NOT_READY;

        if (released == KEEL_RESULT_WRONG_THREAD || released == KEEL_RESULT_BUSY)
            return released;

        const auto fail = [&](KeelResult result)
        {
            if (held->generation == generation)
                held->status.Set(result);

            return result;
        };

        if (!context || !context->accepting_resources.load(std::memory_order_acquire) || !api)
            return fail(KEEL_RESULT_NOT_READY);

        struct Candidate
        {
            std::shared_ptr<keels2::detail::ContextState> context;
            const KeelEntitiesApi* api;
            KeelEntityHandle handle{};

            ~Candidate()
            {
                if (handle)
                    api->release(context->plugin, handle);
            }
        } candidate{context, api};
        const KeelEntityConstructionApi* construction{};
        auto result = factory(candidate.handle, construction);

        if (result != KEEL_RESULT_OK)
            return fail(result);

        if (!held->alive || held->generation != generation)
            return KEEL_RESULT_NOT_READY;

        if (!candidate.handle)
            return fail(KEEL_RESULT_INCOMPATIBLE);

        KeelEntityInfo info{};
        info.size = sizeof(info);
        result = created ? construction->describe(context->plugin, candidate.handle, &info)
                         : api->describe(context->plugin, candidate.handle, &info);

        if (result != KEEL_RESULT_OK)
            return fail(result);

        if (!held->alive || held->generation != generation ||
            !context->accepting_resources.load(std::memory_order_acquire))
            return KEEL_RESULT_NOT_READY;

        if (info.size != sizeof(info) || info.reserved || info.index < 0 ||
            info.source2_handle == KEELS2_INVALID_SOURCE2_ENTITY_HANDLE || !info.epoch ||
            (index >= 0 && info.index != index) ||
            (source != KEELS2_INVALID_SOURCE2_ENTITY_HANDLE && info.source2_handle != source))
            return fail(KEEL_RESULT_INCOMPATIBLE);

        held->value = {context, api, construction, std::exchange(candidate.handle, 0), info};
        return fail(KEEL_RESULT_OK);
    }

    std::shared_ptr<keels2::detail::ContextState> context_;
    const KeelEntitiesApi* api_{};
};
}
#endif
