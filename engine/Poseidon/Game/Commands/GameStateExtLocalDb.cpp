#include <Poseidon/Core/LocalDb/LocalDbStore.hpp>
#include <Poseidon/Core/Profile/ProfileManager.hpp>
#include <Poseidon/Core/Global.hpp>
#include <Poseidon/Foundation/Common/GamePaths.hpp>
#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>

#include <map>
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

std::string ActiveLocalDbProfileDirectory()
{
    const GamePaths& paths = GamePaths::Instance();
    if (!paths.IsInitialized() || paths.UserDir().empty() || Glob.header.playerName.GetLength() == 0)
        return {};

    return Poseidon::ProfileManager::GetProfileDirPath(paths.UserDir(), std::string((const char*)Glob.header.playerName));
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

GameValue StringArrayValue(const GameState* state, const std::vector<std::string>& strings)
{
    GameValue value = state->CreateGameValue(GameArray);
    GameArrayType& array = value;
    for (const std::string& string : strings)
        array.Add(GameValue(string.c_str()));
    return value;
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

    const GameArrayType& array = arg;
    return GameValue(ActiveLocalDbStore().Save(GameStringToStdString(array[0]), GameStringToStdString(array[1]),
                                             GameStringToStdString(array[2])));
}

GameValue LocalDbLoad(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue("");

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

    return StringArrayValue(state, ActiveLocalDbStore().List(GameStringToStdString(arg)));
}

GameValue LocalDbCacheLoad(const GameState* state, GameValuePar arg)
{
    if (!CheckLocalDbArrayArg(state, arg, 2))
        return GameValue(false);

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

    const GameArrayType& array = arg;
    Poseidon::LocalDb::Store store = ActiveLocalDbStore();
    const std::string database = GameStringToStdString(array[0]);
    const std::string key = GameStringToStdString(array[1]);
    LocalDbCache().erase(CacheKey(store, database, key));
    return GameValue(true);
}

GameValue LocalDbCacheClear(const GameState* /*state*/)
{
    LocalDbCache().clear();
    return GameValue(true);
}
