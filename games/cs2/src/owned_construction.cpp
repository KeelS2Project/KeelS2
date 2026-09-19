#include <keels2/cs2/owned_construction.h>
#include <algorithm>
#include <cmath>

namespace keels2::cs2 {
namespace {
bool CopyText(const char* input, std::size_t maximum, std::string& output)
{
    if (!input)
        return false;

    std::size_t length{};

    while (length <= maximum && input[length])
        ++length;

    if (length > maximum)
        return false;

    output.assign(input,length);
    return true;
}

bool SameKey(const std::string& left, const std::string& right)
{
    const auto lower = [](unsigned char c)
    {
        return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    };
    return left.size() == right.size() && std::equal(left.begin(),
                                                     left.end(),
                                                     right.begin(),
                                                     [&](unsigned char a, unsigned char b)
                                                     {
                                                         return lower(a) == lower(b);
                                                     });
}
}

OwnedConstructions::~OwnedConstructions()
{
    Reset();
}

KeelCs2EntityKeyValue OwnedConstructions::Key::View() const noexcept
{
    auto result = value;
    result.name = name.c_str();
    result.string_value = text.c_str();
    return result;
}

OwnedConstructions::Operation::Operation(OwnedConstructions& owner, std::shared_ptr<Record> value)
    : store(owner), record(std::move(value)), was_busy(record->busy)
{
    ++store.depth_;
    record->busy = true;
}

OwnedConstructions::Operation::~Operation()
{
    record->busy = was_busy;

    if (record->closed && !was_busy)
        static_cast<void>(store.Finish(record));

    --store.depth_;
}

std::shared_ptr<OwnedConstructions::Record> OwnedConstructions::Find(std::uint64_t token) const noexcept
{
    if (token)
        for (const auto& record : records_)
            if (record && !record->closed && record->token == token)
                return record;

    return {};
}

unsigned OwnedConstructions::Count() const noexcept
{
    return static_cast<unsigned>(std::count_if(records_.begin(),
                                               records_.end(),
                                               [](const auto& record)
                                               {
                                                   return !!record;
                                               }));
}

void OwnedConstructions::Detach(const std::shared_ptr<Record>& record) noexcept
{
    record->closed = true;

    for (auto& slot : records_)
        if (slot == record)
        {
            slot.reset();
            break;
        }
}

KeelResult OwnedConstructions::Finish(const std::shared_ptr<Record>& record) noexcept
{
    if (record->consumed || !record->created)
        return KEEL_RESULT_OK;

    record->consumed = true;

    try
    {
        return backend_.Cancel(record->identity);
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult OwnedConstructions::Begin(std::uint64_t token, std::shared_ptr<Record>& record)
{
    const auto ready = backend_.Ready();

    if (ready != KEEL_RESULT_OK)
        return ready;

    record = Find(token);

    if (!record)
        return KEEL_RESULT_NOT_FOUND;

    if (resetting_ || record->busy || depth_ >= 8)
        return KEEL_RESULT_BUSY;

    return KEEL_RESULT_OK;
}

KeelResult OwnedConstructions::Build(const Record& record, const std::vector<Key>& keys, Values& output)
{
    std::array<KeelCs2EntityKeyValue,KEELS2_CS2_KEY_MAX_COUNT> views{};

    for (std::size_t i = 0; i < keys.size(); ++i)
        views[i] = keys[i].View();

    void* value{};
    const auto result = KeelCs2KeyValues_Build(
        record.class_name.c_str(), views.data(), static_cast<std::uint32_t>(keys.size()), &value);

    output.reset(value);
    return result;
}

KeelResult OwnedConstructions::CopyKey(const KeelCs2EntityKeyValue& input, Key& key)
{
    if (input.size != sizeof(input) || !CopyText(input.name, KEELS2_CS2_KEY_MAX_NAME, key.name))
        return KEEL_RESULT_INVALID_ARGUMENT;

    key.value = input;
    key.value.name = key.value.string_value = nullptr;

    if (input.type == KEELS2_CS2_KEY_STRING && !CopyText(input.string_value, KEELS2_CS2_KEY_MAX_STRING, key.text))
        return KEEL_RESULT_INVALID_ARGUMENT;

    return KEEL_RESULT_OK;
}

KeelResult
OwnedConstructions::Create(const char* class_name, std::uint64_t& token, host::GameEntityIdentity& identity) noexcept
{
    token = 0;
    identity = {};

    try {
        std::string name;

        if (!CopyText(class_name, KEELS2_CS2_KEY_MAX_NAME, name))
            return KEEL_RESULT_INVALID_ARGUMENT;

        const auto ready = backend_.Ready();

        if (ready != KEEL_RESULT_OK)
            return ready;

        if (resetting_ || depth_ >= 8 || next_token_ == UINT64_MAX)
            return KEEL_RESULT_BUSY;

        auto slot = std::find(records_.begin(),records_.end(),nullptr);

        if (slot == records_.end())
            return KEEL_RESULT_BUSY;

        auto record = std::make_shared<Record>();
        record->class_name = std::move(name);
        record->token = next_token_++;
        auto result = Build(*record,record->keys,record->values);

        if (result != KEEL_RESULT_OK)
            return result;

        *slot = record;
        Operation operation(*this,record);

        try {
            result = backend_.Create(record->class_name.c_str(),record->identity);
            record->created = result == KEEL_RESULT_OK;

            if (record->created) for (const auto& other : records_) {
                    if (!other || other == record || !other->created || other->consumed || other->closed)
                        continue;

                    if (other->identity.index == record->identity.index &&
                        other->identity.source2_handle == record->identity.source2_handle &&
                        other->identity.epoch == record->identity.epoch)
                    {
                        record->created = false;
                        result = KEEL_RESULT_ALREADY_EXISTS;
                        break;
                }
            }

            if (record->created && !record->closed)
                result = backend_.Validate(record->identity);
        }
        catch (...)
        {
            result = KEEL_RESULT_ENGINE_FAILURE;
        }

        if (result != KEEL_RESULT_OK || record->closed) {
            Detach(record);
            return result == KEEL_RESULT_OK ? KEEL_RESULT_NOT_FOUND : result;
        }

        token = record->token;
        identity = record->identity;
        return KEEL_RESULT_OK;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult OwnedConstructions::Describe(std::uint64_t token, host::GameEntityIdentity& identity) noexcept
{
    identity = {};

    try {
        const auto ready = backend_.Ready();

        if (ready != KEEL_RESULT_OK)
            return ready;

        auto record = Find(token);

        if (!record || !record->created || record->consumed)
            return KEEL_RESULT_NOT_FOUND;

        const auto result = backend_.Validate(record->identity);

        if (result == KEEL_RESULT_OK && !record->closed)
            identity = record->identity;

        return record->closed ? KEEL_RESULT_NOT_FOUND : result;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult OwnedConstructions::Set(std::uint64_t token, const KeelCs2EntityKeyValue& input) noexcept
{
    try {
        Key key;
        auto result = CopyKey(input,key);

        if (result != KEEL_RESULT_OK)
            return result;

        std::shared_ptr<Record> record;
        result = Begin(token,record);

        if (result != KEEL_RESULT_OK)
            return result;

        Operation operation(*this,record);
        result = backend_.Validate(record->identity);

        if (result != KEEL_RESULT_OK || record->closed)
            return record->closed ? KEEL_RESULT_NOT_FOUND : result;

        auto keys = record->keys;
        auto found = std::find_if(keys.begin(),
                                  keys.end(),
                                  [&](const auto& old)
                                  {
                                      return SameKey(old.name, key.name);
                                  });

        if (found != keys.end())
            *found = std::move(key);
        else {
            if (keys.size() >= KEELS2_CS2_KEY_MAX_COUNT)
                return KEEL_RESULT_BUSY;

            keys.push_back(std::move(key));
        }

        Values value;
        result = Build(*record,keys,value);

        if (result != KEEL_RESULT_OK || record->closed)
            return record->closed ? KEEL_RESULT_NOT_FOUND : result;

        record->keys = std::move(keys);
        record->values = std::move(value);
        return KEEL_RESULT_OK;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult OwnedConstructions::Teleport(std::uint64_t token, const KeelEntityTeleport& input) noexcept
{
    if (input.size != sizeof(input) || !input.flags || (input.flags & ~7u))
        return KEEL_RESULT_INVALID_ARGUMENT;

    const auto request = input;
    const float* vectors[]{request.position,request.angles,request.velocity};

    for (unsigned i = 0; i < 3; ++i) if (request.flags & (1u<<i))
            for (unsigned j = 0; j < 3; ++j)
                if (!std::isfinite(vectors[i][j]))
                    return KEEL_RESULT_INVALID_ARGUMENT;

    try {
        std::shared_ptr<Record> record;
        auto result = Begin(token, record);

        if (result != KEEL_RESULT_OK)
            return result;

        Operation operation(*this,record);
        result = backend_.Validate(record->identity);

        if (result != KEEL_RESULT_OK || record->closed)
            return record->closed ? KEEL_RESULT_NOT_FOUND : result;

        result = backend_.Teleport(record->identity,request);
        return record->closed ? KEEL_RESULT_NOT_FOUND : result;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult OwnedConstructions::Spawn(std::uint64_t token, KeelBool& invoked) noexcept
{
    invoked = KEEL_FALSE;

    try {
        std::shared_ptr<Record> record;
        auto result = Begin(token, record);

        if (result != KEEL_RESULT_OK)
            return result;

        Operation operation(*this,record);
        result = backend_.Validate(record->identity);

        if (result != KEEL_RESULT_OK || record->closed)
            return record->closed ? KEEL_RESULT_NOT_FOUND : result;

        try
        {
            result = backend_.Spawn(record->identity, record->values.get(), invoked);
        }
        catch (...)
        {
            result = KEEL_RESULT_ENGINE_FAILURE;
        }

        if (invoked)
        {
            record->consumed = true;
            Detach(record);
        }

        return result;
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult OwnedConstructions::Cancel(std::uint64_t token) noexcept
{
    try {
        const auto ready = backend_.Ready();

        if (ready != KEEL_RESULT_OK)
            return ready;

        auto record = Find(token);

        if (!record)
            return KEEL_RESULT_NOT_FOUND;

        Detach(record);

        if (record->busy)
            return KEEL_RESULT_OK;

        Operation operation(*this,record);
        return Finish(record);
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

KeelResult OwnedConstructions::Visit(std::uint64_t token, const char* class_name,
    KeelEntityAccessCallback callback, void* data) noexcept
{
    try {
        std::string name;

        if (!callback || !CopyText(class_name, 255, name) || name.empty())
            return KEEL_RESULT_INVALID_ARGUMENT;

        auto result = backend_.Ready();

        if (result != KEEL_RESULT_OK)
            return result;

        auto record = Find(token);

        if (!record || !record->created || record->consumed)
            return KEEL_RESULT_NOT_FOUND;

        if (resetting_ || depth_ >= 8)
            return KEEL_RESULT_BUSY;

        Operation operation(*this,record);
        result = backend_.Validate(record->identity);

        if (result != KEEL_RESULT_OK || record->closed)
            return record->closed ? KEEL_RESULT_NOT_FOUND : result;

        return backend_.Visit(record->identity,name.c_str(),callback,data);
    }
    catch (...)
    {
        return KEEL_RESULT_ENGINE_FAILURE;
    }
}

void OwnedConstructions::Reset() noexcept
{
    ++resetting_;
    auto old = std::move(records_);
    records_ = {};

    for (auto& record : old)
        if (record)
            record->closed = true;

    for (auto& record : old) if (record && !record->busy) {
        Operation operation(*this,record);
        static_cast<void>(Finish(record));
    }

    --resetting_;
}
}
