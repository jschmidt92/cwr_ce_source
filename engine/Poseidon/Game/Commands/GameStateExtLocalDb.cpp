#include <Poseidon/Core/LocalDb/LocalDbStore.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>

#include <cjson/cJSON.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::string GameStringToStdString(GameValuePar value)
{
    return std::string(((RString)(GameStringType)value).Data());
}

bool CheckLocalDbArrayArg(const GameState* state, GameValuePar arg, int size)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != size)
    {
        state->SetError(EvalDim, array.Size(), size);
        return false;
    }

    for (int i = 0; i < size; ++i)
    {
        if (array[i].GetType() != GameString)
        {
            state->TypeError(GameString, array[i].GetType());
            return false;
        }
    }
    return true;
}

bool CheckLocalDbUpdateArg(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return false;
    }
    if (array[2].GetType() != GameString && array[2].GetType() != GameCode)
    {
        state->TypeError(GameString | GameCode, array[2].GetType());
        return false;
    }
    return true;
}

bool CheckLocalDbFindArg(const GameState* state, GameValuePar arg, bool path)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (!path && array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return false;
    }
    if (path)
    {
        if (array[1].GetType() != GameArray)
        {
            state->TypeError(GameArray, array[1].GetType());
            return false;
        }

        const GameArrayType& pathArray = array[1];
        for (int i = 0; i < pathArray.Size(); ++i)
        {
            if (pathArray[i].GetType() != GameString)
            {
                state->TypeError(GameString, pathArray[i].GetType());
                return false;
            }
        }
    }
    return true;
}

bool CheckLocalDbIndexArg(const GameState* state, GameValuePar arg, bool path)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return false;
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return false;
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (!path && array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return false;
    }
    if (path)
    {
        if (array[1].GetType() != GameArray)
        {
            state->TypeError(GameArray, array[1].GetType());
            return false;
        }

        const GameArrayType& pathArray = array[1];
        for (int i = 0; i < pathArray.Size(); ++i)
        {
            if (pathArray[i].GetType() != GameString)
            {
                state->TypeError(GameString, pathArray[i].GetType());
                return false;
            }
        }
    }
    return true;
}

std::string ActiveLocalDbProfileDirectory()
{
    const GamePaths& paths = GamePaths::Instance();
    if (!paths.IsInitialized() || paths.UserDir().empty() || Glob.header.playerName.GetLength() == 0)
        return {};

    return Poseidon::ProfileManager::GetProfileDirPath(paths.UserDir(),
                                                       std::string((const char*)Glob.header.playerName));
}

Poseidon::LocalDb::Store ActiveLocalDbStore()
{
    return Poseidon::LocalDb::Store(ActiveLocalDbProfileDirectory());
}

std::string CacheKey(const Poseidon::LocalDb::Store& store, const std::string& database, const std::string& key)
{
    return store.Root() + "\n" + database + "\n" + key;
}

struct CacheRecord
{
    std::string root;
    std::string database;
    std::string key;
    std::string json;
};

std::map<std::string, CacheRecord>& LocalDbCache()
{
    static std::map<std::string, CacheRecord> cache;
    return cache;
}

std::recursive_mutex& LocalDbMutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

GameValue StringArrayValue(const GameState* state, const std::vector<std::string>& strings)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    for (const std::string& string : strings)
        array.Add(GameValue(string.c_str()));
    return value;
}

cJSON* GameValueToJson(GameValuePar value)
{
    if (value.GetNil())
        return cJSON_CreateNull();

    const GameType type = value.GetType();
    if (type == GameString)
        return cJSON_CreateString(GameStringToStdString(value).c_str());
    if (type == GameScalar)
        return cJSON_CreateNumber(static_cast<double>(static_cast<float>(value)));
    if (type == GameBool)
        return cJSON_CreateBool(static_cast<bool>(value));
    if (type == GameArray)
    {
        cJSON* jsonArray = cJSON_CreateArray();
        if (!jsonArray)
            return nullptr;

        const GameArrayType& array = value;
        for (int i = 0; i < array.Size(); ++i)
        {
            cJSON* item = GameValueToJson(array[i]);
            if (!item || !cJSON_AddItemToArray(jsonArray, item))
            {
                cJSON_Delete(item);
                cJSON_Delete(jsonArray);
                return nullptr;
            }
        }
        return jsonArray;
    }
    return cJSON_CreateNull();
}

GameValue JsonArrayToGameValue(const GameState* state, const cJSON* item)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;

    if (!cJSON_IsArray(item))
        return value;

    const cJSON* element = nullptr;
    cJSON_ArrayForEach(element, item)
    {
        if (cJSON_IsArray(element))
            array.Add(JsonArrayToGameValue(state, element));
        else if (cJSON_IsString(element))
            array.Add(GameValue(element->valuestring ? element->valuestring : ""));
        else if (cJSON_IsNumber(element))
            array.Add(GameValue(static_cast<float>(element->valuedouble)));
        else if (cJSON_IsBool(element))
            array.Add(GameValue(cJSON_IsTrue(element) != 0));
        else
            array.Add(GameValue());
    }

    return value;
}

GameValue JsonValueToGameValue(const GameState* state, const cJSON* item)
{
    if (!item || cJSON_IsNull(item))
        return GameValue();
    if (cJSON_IsArray(item))
        return JsonArrayToGameValue(state, item);
    if (cJSON_IsString(item))
        return GameValue(item->valuestring ? item->valuestring : "");
    if (cJSON_IsNumber(item))
        return GameValue(static_cast<float>(item->valuedouble));
    if (cJSON_IsBool(item))
        return GameValue(cJSON_IsTrue(item) != 0);
    return GameValue();
}

const cJSON* JsonPathItem(const cJSON* root, const GameArrayType& path)
{
    const cJSON* current = root;
    for (int i = 0; current && i < path.Size(); ++i)
    {
        if (!cJSON_IsObject(current))
            return nullptr;
        current = cJSON_GetObjectItemCaseSensitive(current, GameStringToStdString(path[i]).c_str());
    }
    return current;
}

const cJSON* QueryItem(const cJSON* root, GameValuePar selector, bool path)
{
    if (path)
        return JsonPathItem(root, selector);
    if (!cJSON_IsObject(root))
        return nullptr;
    return cJSON_GetObjectItemCaseSensitive(root, GameStringToStdString(selector).c_str());
}

std::string JsonComparableKey(const cJSON* item)
{
    if (!item)
        return {};

    const char* prefix = "x:";
    if (cJSON_IsString(item))
        return std::string("s:") + (item->valuestring ? item->valuestring : "");
    if (cJSON_IsNumber(item))
        prefix = "n:";
    else if (cJSON_IsBool(item))
        return std::string("b:") + (cJSON_IsTrue(item) ? "1" : "0");
    else if (cJSON_IsArray(item))
        prefix = "a:";
    else if (cJSON_IsObject(item))
        prefix = "o:";
    else if (cJSON_IsNull(item))
        return "z:null";

    char* printed = cJSON_PrintUnformatted(item);
    if (!printed)
        return {};

    std::string key = std::string(prefix) + printed;
    cJSON_free(printed);
    return key;
}

std::string ComparableKeyForGameValue(GameValuePar value)
{
    cJSON* json = GameValueToJson(value);
    const std::string key = JsonComparableKey(json);
    cJSON_Delete(json);
    return key;
}

GameValue LocalDbFindImpl(const GameState* state, GameValuePar arg, bool path)
{
    if (!CheckLocalDbFindArg(state, arg, path))
        return state->CreateGameValue(GameArray);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string wanted = ComparableKeyForGameValue(array[2]);
    std::vector<std::string> matches;

    for (const std::string& key : store.List(database))
    {
        std::string json;
        if (!store.Load(database, key, json))
            continue;

        cJSON* root = cJSON_Parse(json.c_str());
        const std::string actual = JsonComparableKey(QueryItem(root, array[1], path));
        cJSON_Delete(root);

        if (!actual.empty() && actual == wanted)
            matches.push_back(key);
    }

    return StringArrayValue(state, matches);
}

GameValue LocalDbIndexImpl(const GameState* state, GameValuePar arg, bool path)
{
    if (!CheckLocalDbIndexArg(state, arg, path))
        return state->CreateGameValue(GameArray);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    struct IndexBucket
    {
        GameValue value;
        std::vector<std::string> keys;
    };

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    std::map<std::string, IndexBucket> buckets;

    for (const std::string& key : store.List(database))
    {
        std::string json;
        if (!store.Load(database, key, json))
            continue;

        cJSON* root = cJSON_Parse(json.c_str());
        const cJSON* item = QueryItem(root, array[1], path);
        const std::string bucketKey = JsonComparableKey(item);
        if (!bucketKey.empty())
        {
            IndexBucket& bucket = buckets[bucketKey];
            if (bucket.keys.empty())
                bucket.value = JsonValueToGameValue(state, item);
            bucket.keys.push_back(key);
        }
        cJSON_Delete(root);
    }

    GameValue result = state->CreateGameValue(GameArray);
    GameArrayType& resultArray = result;
    for (const auto& entry : buckets)
    {
        GameValue pair = state->CreateGameValue(GameArray);
        GameArrayType& pairArray = pair;
        pairArray.Resize(2);
        pairArray[0] = entry.second.value;
        pairArray[1] = StringArrayValue(state, entry.second.keys);
        resultArray.Add(pair);
    }
    return result;
}

enum class AsyncLocalDbOp
{
    Save,
    Load,
    Remove,
    Exists,
    List,
    Find,
    FindPath,
    Index,
    IndexPath,
    CacheFlush,
    CacheFlushAll,
};

enum class AsyncLocalDbStatus
{
    Queued,
    Running,
    Done,
};

struct AsyncJsonValue
{
    enum class Kind
    {
        Empty,
        String,
        Number,
        Bool,
        Json,
    };

    Kind kind = Kind::Empty;
    std::string text;
    double number = 0.0;
    bool boolean = false;
};

struct AsyncIndexBucket
{
    AsyncJsonValue value;
    std::vector<std::string> keys;
};

struct AsyncLocalDbResult
{
    bool ok = false;
    AsyncJsonValue value;
    std::vector<std::string> strings;
    std::vector<AsyncIndexBucket> index;
};

struct AsyncLocalDbJob
{
    int id = 0;
    AsyncLocalDbOp op = AsyncLocalDbOp::Load;
    std::string profileDirectory;
    std::string database;
    std::string key;
    std::string json;
    std::string selector;
    std::vector<std::string> selectorPath;
    std::string wanted;
};

struct AsyncLocalDbRecord
{
    int id = 0;
    AsyncLocalDbOp op = AsyncLocalDbOp::Load;
    AsyncLocalDbStatus status = AsyncLocalDbStatus::Queued;
    AsyncLocalDbResult result;
};

const char* AsyncLocalDbOpName(AsyncLocalDbOp op)
{
    switch (op)
    {
        case AsyncLocalDbOp::Save:
            return "save";
        case AsyncLocalDbOp::Load:
            return "load";
        case AsyncLocalDbOp::Remove:
            return "remove";
        case AsyncLocalDbOp::Exists:
            return "exists";
        case AsyncLocalDbOp::List:
            return "list";
        case AsyncLocalDbOp::Find:
            return "find";
        case AsyncLocalDbOp::FindPath:
            return "findPath";
        case AsyncLocalDbOp::Index:
            return "index";
        case AsyncLocalDbOp::IndexPath:
            return "indexPath";
        case AsyncLocalDbOp::CacheFlush:
            return "cacheFlush";
        case AsyncLocalDbOp::CacheFlushAll:
            return "cacheFlushAll";
    }
    return "unknown";
}

const char* AsyncLocalDbStatusName(AsyncLocalDbStatus status)
{
    switch (status)
    {
        case AsyncLocalDbStatus::Queued:
            return "queued";
        case AsyncLocalDbStatus::Running:
            return "running";
        case AsyncLocalDbStatus::Done:
            return "done";
    }
    return "unknown";
}

AsyncJsonValue CaptureJsonValue(const cJSON* item)
{
    AsyncJsonValue value;
    if (!item || cJSON_IsNull(item))
        return value;
    if (cJSON_IsString(item))
    {
        value.kind = AsyncJsonValue::Kind::String;
        value.text = item->valuestring ? item->valuestring : "";
        return value;
    }
    if (cJSON_IsNumber(item))
    {
        value.kind = AsyncJsonValue::Kind::Number;
        value.number = item->valuedouble;
        return value;
    }
    if (cJSON_IsBool(item))
    {
        value.kind = AsyncJsonValue::Kind::Bool;
        value.boolean = cJSON_IsTrue(item) != 0;
        return value;
    }

    char* printed = cJSON_PrintUnformatted(item);
    if (printed)
    {
        value.kind = AsyncJsonValue::Kind::Json;
        value.text = printed;
        cJSON_free(printed);
    }
    return value;
}

GameValue AsyncJsonValueToGameValue(const GameState* state, const AsyncJsonValue& value)
{
    switch (value.kind)
    {
        case AsyncJsonValue::Kind::String:
            return GameValue(value.text.c_str());
        case AsyncJsonValue::Kind::Json:
        {
            cJSON* root = cJSON_Parse(value.text.c_str());
            GameValue result = JsonValueToGameValue(state, root);
            cJSON_Delete(root);
            return result;
        }
        case AsyncJsonValue::Kind::Number:
            return GameValue(static_cast<float>(value.number));
        case AsyncJsonValue::Kind::Bool:
            return GameValue(value.boolean);
        case AsyncJsonValue::Kind::Empty:
            return GameValue();
    }
    return GameValue();
}

const cJSON* QueryItemForAsync(const cJSON* root, const AsyncLocalDbJob& job)
{
    if (job.op == AsyncLocalDbOp::FindPath || job.op == AsyncLocalDbOp::IndexPath)
    {
        const cJSON* current = root;
        for (const std::string& pathPart : job.selectorPath)
        {
            if (!current || !cJSON_IsObject(current))
                return nullptr;
            current = cJSON_GetObjectItemCaseSensitive(current, pathPart.c_str());
        }
        return current;
    }

    if (!cJSON_IsObject(root))
        return nullptr;
    return cJSON_GetObjectItemCaseSensitive(root, job.selector.c_str());
}

AsyncLocalDbResult ExecuteAsyncLocalDbJob(const AsyncLocalDbJob& job)
{
    AsyncLocalDbResult result;
    Poseidon::LocalDb::Store store(job.profileDirectory);
    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    switch (job.op)
    {
        case AsyncLocalDbOp::Save:
            result.ok = store.Save(job.database, job.key, job.json);
            return result;
        case AsyncLocalDbOp::Load:
        {
            result.ok = store.Load(job.database, job.key, result.value.text);
            if (result.ok)
                result.value.kind = AsyncJsonValue::Kind::String;
            return result;
        }
        case AsyncLocalDbOp::Remove:
            result.ok = store.Remove(job.database, job.key);
            if (result.ok)
                LocalDbCache().erase(CacheKey(store, job.database, job.key));
            return result;
        case AsyncLocalDbOp::Exists:
            result.ok = true;
            result.value.kind = AsyncJsonValue::Kind::Bool;
            result.value.boolean = store.Exists(job.database, job.key);
            return result;
        case AsyncLocalDbOp::List:
            result.strings = store.List(job.database);
            result.ok = true;
            return result;
        case AsyncLocalDbOp::Find:
        case AsyncLocalDbOp::FindPath:
            for (const std::string& key : store.List(job.database))
            {
                std::string json;
                if (!store.Load(job.database, key, json))
                    continue;

                cJSON* root = cJSON_Parse(json.c_str());
                const std::string actual = JsonComparableKey(QueryItemForAsync(root, job));
                cJSON_Delete(root);
                if (!actual.empty() && actual == job.wanted)
                    result.strings.push_back(key);
            }
            result.ok = true;
            return result;
        case AsyncLocalDbOp::Index:
        case AsyncLocalDbOp::IndexPath:
        {
            std::map<std::string, AsyncIndexBucket> buckets;
            for (const std::string& key : store.List(job.database))
            {
                std::string json;
                if (!store.Load(job.database, key, json))
                    continue;

                cJSON* root = cJSON_Parse(json.c_str());
                const cJSON* item = QueryItemForAsync(root, job);
                const std::string bucketKey = JsonComparableKey(item);
                if (!bucketKey.empty())
                {
                    AsyncIndexBucket& bucket = buckets[bucketKey];
                    if (bucket.keys.empty())
                        bucket.value = CaptureJsonValue(item);
                    bucket.keys.push_back(key);
                }
                cJSON_Delete(root);
            }
            for (auto& entry : buckets)
                result.index.push_back(entry.second);
            result.ok = true;
            return result;
        }
        case AsyncLocalDbOp::CacheFlush:
        {
            const auto found = LocalDbCache().find(CacheKey(store, job.database, job.key));
            result.ok = found != LocalDbCache().end() && store.Save(job.database, job.key, found->second.json);
            return result;
        }
        case AsyncLocalDbOp::CacheFlushAll:
        {
            bool flushed = false;
            result.ok = true;
            for (const auto& entry : LocalDbCache())
            {
                const CacheRecord& record = entry.second;
                if (record.root != store.Root())
                    continue;

                flushed = true;
                result.ok = store.Save(record.database, record.key, record.json) && result.ok;
            }
            result.ok = flushed && result.ok;
            return result;
        }
    }

    return result;
}

class AsyncLocalDbQueue
{
  public:
    AsyncLocalDbQueue() : m_worker(&AsyncLocalDbQueue::WorkerLoop, this) {}

    ~AsyncLocalDbQueue()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_condition.notify_one();
        if (m_worker.joinable())
            m_worker.join();
    }

    int Enqueue(AsyncLocalDbJob job)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        PruneDoneLocked();
        if (m_records.size() >= kMaxAsyncLocalDbRecords)
            return -1;

        job.id = m_nextId++;
        AsyncLocalDbRecord record;
        record.id = job.id;
        record.op = job.op;
        m_records[job.id] = record;
        m_jobs.push_back(job);
        m_condition.notify_one();
        return job.id;
    }

    bool Get(int id, AsyncLocalDbRecord& record) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_records.find(id);
        if (found == m_records.end())
            return false;
        record = found->second;
        return true;
    }

    std::vector<AsyncLocalDbRecord> List() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<AsyncLocalDbRecord> records;
        for (const auto& entry : m_records)
            records.push_back(entry.second);
        return records;
    }

    bool Clear(int id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_records.find(id);
        if (found == m_records.end() || found->second.status != AsyncLocalDbStatus::Done)
            return false;
        m_records.erase(found);
        return true;
    }

  private:
    static constexpr size_t kMaxAsyncLocalDbRecords = 1024;

    void PruneDoneLocked()
    {
        while (m_records.size() >= kMaxAsyncLocalDbRecords)
        {
            auto done = m_records.end();
            for (auto it = m_records.begin(); it != m_records.end(); ++it)
            {
                if (it->second.status == AsyncLocalDbStatus::Done)
                {
                    done = it;
                    break;
                }
            }
            if (done == m_records.end())
                return;
            m_records.erase(done);
        }
    }

    void WorkerLoop()
    {
        while (true)
        {
            AsyncLocalDbJob job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [&]() { return m_stopping || !m_jobs.empty(); });
                if (m_stopping && m_jobs.empty())
                    return;
                job = m_jobs.front();
                m_jobs.pop_front();
                m_records[job.id].status = AsyncLocalDbStatus::Running;
            }

            AsyncLocalDbResult result = ExecuteAsyncLocalDbJob(job);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                AsyncLocalDbRecord& record = m_records[job.id];
                record.status = AsyncLocalDbStatus::Done;
                record.result = result;
            }
        }
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<AsyncLocalDbJob> m_jobs;
    std::map<int, AsyncLocalDbRecord> m_records;
    std::thread m_worker;
    int m_nextId = 1;
    bool m_stopping = false;
};

AsyncLocalDbQueue& LocalDbAsyncQueue()
{
    static AsyncLocalDbQueue queue;
    return queue;
}

GameValue AsyncLocalDbResultValue(const GameState* state, const AsyncLocalDbRecord& record)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(4);
    array[0] = GameValue(static_cast<float>(record.id));
    array[1] = GameValue(AsyncLocalDbStatusName(record.status));
    array[2] = GameValue(record.result.ok);

    if (record.op == AsyncLocalDbOp::Find || record.op == AsyncLocalDbOp::FindPath || record.op == AsyncLocalDbOp::List)
    {
        array[3] = StringArrayValue(state, record.result.strings);
    }
    else if (record.op == AsyncLocalDbOp::Index || record.op == AsyncLocalDbOp::IndexPath)
    {
        GameValue indexValue = state->CreateGameValue(GameArray);
        GameArrayType& indexArray = indexValue;
        for (const AsyncIndexBucket& bucket : record.result.index)
        {
            GameValue pair = state->CreateGameValue(GameArray);
            GameArrayType& pairArray = pair;
            pairArray.Resize(2);
            pairArray[0] = AsyncJsonValueToGameValue(state, bucket.value);
            pairArray[1] = StringArrayValue(state, bucket.keys);
            indexArray.Add(pair);
        }
        array[3] = indexValue;
    }
    else
    {
        array[3] = AsyncJsonValueToGameValue(state, record.result.value);
    }
    return value;
}

GameValue AsyncLocalDbJobInfoValue(const GameState* state, const AsyncLocalDbRecord& record)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    array.Resize(4);
    array[0] = GameValue(static_cast<float>(record.id));
    array[1] = GameValue(AsyncLocalDbOpName(record.op));
    array[2] = GameValue(AsyncLocalDbStatusName(record.status));
    array[3] = GameValue(record.status == AsyncLocalDbStatus::Done ? record.result.ok : false);
    return value;
}

bool FillAsyncDatabaseKeyJob(const GameState* state, GameValuePar arg, int size, AsyncLocalDbOp op,
                             AsyncLocalDbJob& job)
{
    if (!CheckLocalDbArrayArg(state, arg, size))
        return false;

    const GameArrayType& array = arg;
    job.op = op;
    job.profileDirectory = ActiveLocalDbProfileDirectory();
    job.database = GameStringToStdString(array[0]);
    job.key = GameStringToStdString(array[1]);
    if (size == 3)
        job.json = GameStringToStdString(array[2]);
    return true;
}

bool FillAsyncFindJob(const GameState* state, GameValuePar arg, bool path, AsyncLocalDbJob& job)
{
    if (!CheckLocalDbFindArg(state, arg, path))
        return false;

    const GameArrayType& array = arg;
    job.op = path ? AsyncLocalDbOp::FindPath : AsyncLocalDbOp::Find;
    job.profileDirectory = ActiveLocalDbProfileDirectory();
    job.database = GameStringToStdString(array[0]);
    job.wanted = ComparableKeyForGameValue(array[2]);
    if (path)
    {
        const GameArrayType& pathArray = array[1];
        for (int i = 0; i < pathArray.Size(); ++i)
            job.selectorPath.push_back(GameStringToStdString(pathArray[i]));
    }
    else
    {
        job.selector = GameStringToStdString(array[1]);
    }
    return true;
}

bool FillAsyncIndexJob(const GameState* state, GameValuePar arg, bool path, AsyncLocalDbJob& job)
{
    if (!CheckLocalDbIndexArg(state, arg, path))
        return false;

    const GameArrayType& array = arg;
    job.op = path ? AsyncLocalDbOp::IndexPath : AsyncLocalDbOp::Index;
    job.profileDirectory = ActiveLocalDbProfileDirectory();
    job.database = GameStringToStdString(array[0]);
    if (path)
    {
        const GameArrayType& pathArray = array[1];
        for (int i = 0; i < pathArray.Size(); ++i)
            job.selectorPath.push_back(GameStringToStdString(pathArray[i]));
    }
    else
    {
        job.selector = GameStringToStdString(array[1]);
    }
    return true;
}
} // namespace

GameValue LocalDbRoot(const GameState* /*state*/)
{
    return GameValue(ActiveLocalDbStore().Root().c_str());
}

GameValue LocalDbSave(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 3))
        return GameValue(false);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    return GameValue(ActiveLocalDbStore().Save(GameStringToStdString(array[0]), GameStringToStdString(array[1]),
                                               GameStringToStdString(array[2])));
}

GameValue LocalDbLoad(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue("");

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    std::string json;
    if (!ActiveLocalDbStore().Load(GameStringToStdString(array[0]), GameStringToStdString(array[1]), json))
        return GameValue("");
    return GameValue(json.c_str());
}

GameValue LocalDbRemove(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue(false);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string key = GameStringToStdString(array[1]);
    LocalDbCache().erase(CacheKey(store, database, key));
    return GameValue(store.Remove(database, key));
}

GameValue LocalDbExists(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue(false);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    return GameValue(ActiveLocalDbStore().Exists(GameStringToStdString(array[0]), GameStringToStdString(array[1])));
}

GameValue LocalDbList(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
    {
        state->TypeError(GameString, arg.GetType());
        return state->CreateGameValue(GameArray);
    }

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    return StringArrayValue(state, ActiveLocalDbStore().List(GameStringToStdString(arg)));
}

GameValue LocalDbFind(const GameState* state, GameValuePar arg)
{
    return LocalDbFindImpl(state, arg, false);
}

GameValue LocalDbFindPath(const GameState* state, GameValuePar arg)
{
    return LocalDbFindImpl(state, arg, true);
}

GameValue LocalDbIndex(const GameState* state, GameValuePar arg)
{
    return LocalDbIndexImpl(state, arg, false);
}

GameValue LocalDbIndexPath(const GameState* state, GameValuePar arg)
{
    return LocalDbIndexImpl(state, arg, true);
}

GameValue LocalDbUpdate(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbUpdateArg(state, arg))
        return GameValue(false);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string key = GameStringToStdString(array[1]);

    std::string current;
    store.Load(database, key, current);

    GameVarSpace local(state->GetContext());
    state->BeginContext(&local);
    state->VarSetLocal("_this", GameValue(current.c_str()), true);
    GameValue updated = state->EvaluateMultiple((RString)(GameStringType)array[2]);
    state->EndContext();

    if (updated.GetType() != GameString)
    {
        state->TypeError(GameString, updated.GetType());
        return GameValue(false);
    }

    const std::string json = GameStringToStdString(updated);
    if (json.empty())
        return GameValue(false);

    const bool saved = store.Save(database, key, json);
    if (saved)
    {
        const std::string cacheKey = CacheKey(store, database, key);
        const auto found = LocalDbCache().find(cacheKey);
        if (found != LocalDbCache().end())
            found->second.json = json;
    }
    return GameValue(saved);
}

GameValue LocalDbCacheLoad(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue(false);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string key = GameStringToStdString(array[1]);

    std::string json;
    if (!store.Load(database, key, json))
        return GameValue(false);

    LocalDbCache()[CacheKey(store, database, key)] = {store.Root(), database, key, json};
    return GameValue(true);
}

GameValue LocalDbCacheGet(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue("");

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string key = GameStringToStdString(array[1]);

    const auto found = LocalDbCache().find(CacheKey(store, database, key));
    if (found == LocalDbCache().end())
        return GameValue("");
    return GameValue(found->second.json.c_str());
}

GameValue LocalDbCacheSet(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 3))
        return GameValue(false);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string key = GameStringToStdString(array[1]);
    const std::string json = GameStringToStdString(array[2]);
    if (json.empty())
        return GameValue(false);

    LocalDbCache()[CacheKey(store, database, key)] = {store.Root(), database, key, json};
    return GameValue(true);
}

GameValue LocalDbCacheFlush(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue(false);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string key = GameStringToStdString(array[1]);

    const auto found = LocalDbCache().find(CacheKey(store, database, key));
    if (found == LocalDbCache().end())
        return GameValue(false);
    return GameValue(store.Save(database, key, found->second.json));
}

GameValue LocalDbCacheFlushAll(const GameState* /*state*/)
{
    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    bool ok = true;
    bool flushed = false;

    for (const auto& entry : LocalDbCache())
    {
        const CacheRecord& record = entry.second;
        if (record.root != store.Root())
            continue;

        flushed = true;
        ok = store.Save(record.database, record.key, record.json) && ok;
    }

    return GameValue(flushed && ok);
}

GameValue LocalDbCacheRemove(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue(false);

    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string key = GameStringToStdString(array[1]);
    LocalDbCache().erase(CacheKey(store, database, key));
    return GameValue(true);
}

GameValue LocalDbCacheClear(const GameState* /*state*/)
{
    std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());

    LocalDbCache().clear();
    return GameValue(true);
}

GameValue LocalDbAsyncSave(const GameState* state, GameValuePar arg)
{
    AsyncLocalDbJob job;
    if (!FillAsyncDatabaseKeyJob(state, arg, 3, AsyncLocalDbOp::Save, job))
        return GameValue(-1.0f);
    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncLoad(const GameState* state, GameValuePar arg)
{
    AsyncLocalDbJob job;
    if (!FillAsyncDatabaseKeyJob(state, arg, 2, AsyncLocalDbOp::Load, job))
        return GameValue(-1.0f);
    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncRemove(const GameState* state, GameValuePar arg)
{
    AsyncLocalDbJob job;
    if (!FillAsyncDatabaseKeyJob(state, arg, 2, AsyncLocalDbOp::Remove, job))
        return GameValue(-1.0f);

    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncExists(const GameState* state, GameValuePar arg)
{
    AsyncLocalDbJob job;
    if (!FillAsyncDatabaseKeyJob(state, arg, 2, AsyncLocalDbOp::Exists, job))
        return GameValue(-1.0f);
    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncList(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
    {
        state->TypeError(GameString, arg.GetType());
        return GameValue(-1.0f);
    }

    AsyncLocalDbJob job;
    job.op = AsyncLocalDbOp::List;
    job.profileDirectory = ActiveLocalDbProfileDirectory();
    job.database = GameStringToStdString(arg);
    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncFind(const GameState* state, GameValuePar arg)
{
    AsyncLocalDbJob job;
    if (!FillAsyncFindJob(state, arg, false, job))
        return GameValue(-1.0f);
    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncFindPath(const GameState* state, GameValuePar arg)
{
    AsyncLocalDbJob job;
    if (!FillAsyncFindJob(state, arg, true, job))
        return GameValue(-1.0f);
    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncIndex(const GameState* state, GameValuePar arg)
{
    AsyncLocalDbJob job;
    if (!FillAsyncIndexJob(state, arg, false, job))
        return GameValue(-1.0f);
    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncIndexPath(const GameState* state, GameValuePar arg)
{
    AsyncLocalDbJob job;
    if (!FillAsyncIndexJob(state, arg, true, job))
        return GameValue(-1.0f);
    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbCacheAsyncFlush(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue(-1.0f);

    const GameArrayType& array = arg;
    AsyncLocalDbJob job;
    job.op = AsyncLocalDbOp::CacheFlush;
    job.profileDirectory = ActiveLocalDbProfileDirectory();
    job.database = GameStringToStdString(array[0]);
    job.key = GameStringToStdString(array[1]);

    {
        std::lock_guard<std::recursive_mutex> lock(LocalDbMutex());
        Poseidon::LocalDb::Store store(job.profileDirectory);
        if (LocalDbCache().find(CacheKey(store, job.database, job.key)) == LocalDbCache().end())
            return GameValue(-1.0f);
    }

    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbCacheAsyncFlushAll(const GameState* /*state*/)
{
    AsyncLocalDbJob job;
    job.op = AsyncLocalDbOp::CacheFlushAll;
    job.profileDirectory = ActiveLocalDbProfileDirectory();

    return GameValue(static_cast<float>(LocalDbAsyncQueue().Enqueue(job)));
}

GameValue LocalDbAsyncDone(const GameState* /*state*/, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
        return GameValue(false);

    AsyncLocalDbRecord record;
    if (!LocalDbAsyncQueue().Get(static_cast<int>(static_cast<GameScalarType>(arg)), record))
        return GameValue(false);
    return GameValue(record.status == AsyncLocalDbStatus::Done);
}

GameValue LocalDbAsyncResult(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
    {
        state->TypeError(GameScalar, arg.GetType());
        return state->CreateGameValue(GameArray);
    }

    AsyncLocalDbRecord record;
    if (!LocalDbAsyncQueue().Get(static_cast<int>(static_cast<GameScalarType>(arg)), record))
        return state->CreateGameValue(GameArray);
    return AsyncLocalDbResultValue(state, record);
}

GameValue LocalDbAsyncJobs(const GameState* state)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    for (const AsyncLocalDbRecord& record : LocalDbAsyncQueue().List())
        array.Add(AsyncLocalDbJobInfoValue(state, record));
    return value;
}

GameValue LocalDbAsyncClear(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameScalar)
    {
        state->TypeError(GameScalar, arg.GetType());
        return GameValue(false);
    }
    return GameValue(LocalDbAsyncQueue().Clear(static_cast<int>(static_cast<GameScalarType>(arg))));
}
