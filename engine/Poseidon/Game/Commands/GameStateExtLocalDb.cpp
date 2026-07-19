#include <Poseidon/Core/LocalDb/LocalDbStore.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>

#include <cjson/cJSON.h>

#include <map>
#include <mutex>
#include <string>
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
