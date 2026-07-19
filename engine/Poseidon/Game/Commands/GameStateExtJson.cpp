#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Game/Commands/GameStateExtCommon.hpp>

#include <cjson/cJSON.h>

#include <string>

namespace
{
std::string GameStringToStdString(GameValuePar value)
{
    return std::string(((RString)(GameStringType)value).Data());
}

bool CheckJsonFieldArg(const GameState* state, GameValuePar arg)
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
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return false;
    }
    return true;
}

bool CheckJsonPathArg(const GameState* state, GameValuePar arg, int size)
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
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return false;
    }
    if (array[1].GetType() != GameArray)
    {
        state->TypeError(GameArray, array[1].GetType());
        return false;
    }

    const GameArrayType& path = array[1];
    for (int i = 0; i < path.Size(); ++i)
    {
        if (path[i].GetType() != GameString)
        {
            state->TypeError(GameString, path[i].GetType());
            return false;
        }
    }
    return true;
}

GameValue EmptyGameArray(const GameState* state)
{
    return state->CreateGameValue(GameArray);
}

const cJSON* JsonField(const cJSON* root, const std::string& field)
{
    if (!cJSON_IsObject(root))
        return nullptr;
    return cJSON_GetObjectItemCaseSensitive(root, field.c_str());
}

cJSON* JsonPathParent(cJSON* root, const GameArrayType& path, bool createMissing)
{
    if (!cJSON_IsObject(root) || path.Size() <= 0)
        return nullptr;

    cJSON* current = root;
    for (int i = 0; i < path.Size() - 1; ++i)
    {
        const std::string key = GameStringToStdString(path[i]);
        cJSON* child = cJSON_GetObjectItemCaseSensitive(current, key.c_str());
        if (!child)
        {
            if (!createMissing)
                return nullptr;

            child = cJSON_CreateObject();
            if (!child || !cJSON_AddItemToObject(current, key.c_str(), child))
            {
                cJSON_Delete(child);
                return nullptr;
            }
        }
        if (!cJSON_IsObject(child))
        {
            if (!createMissing)
                return nullptr;

            cJSON_DeleteItemFromObjectCaseSensitive(current, key.c_str());
            child = cJSON_CreateObject();
            if (!child || !cJSON_AddItemToObject(current, key.c_str(), child))
            {
                cJSON_Delete(child);
                return nullptr;
            }
        }
        current = child;
    }

    return current;
}

GameValue JsonArrayToGameValue(const GameState* state, const cJSON* item)
{
    GameValue value = EmptyGameArray(state);
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
        cJSON* arrayJson = cJSON_CreateArray();
        if (!arrayJson)
            return nullptr;

        const GameArrayType& array = value;
        for (int i = 0; i < array.Size(); ++i)
        {
            cJSON* item = GameValueToJson(array[i]);
            if (!item || !cJSON_AddItemToArray(arrayJson, item))
            {
                cJSON_Delete(item);
                cJSON_Delete(arrayJson);
                return nullptr;
            }
        }
        return arrayJson;
    }

    return cJSON_CreateNull();
}

std::string JsonToCompactString(cJSON* item)
{
    if (!item)
        return {};

    char* printed = cJSON_PrintUnformatted(item);
    if (!printed)
        return {};

    std::string result(printed);
    cJSON_free(printed);
    return result;
}
} // namespace

GameValue JsonValid(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
    {
        state->TypeError(GameString, arg.GetType());
        return GameValue(false);
    }

    cJSON* root = cJSON_Parse(GameStringToStdString(arg).c_str());
    if (!root)
        return GameValue(false);

    cJSON_Delete(root);
    return GameValue(true);
}

GameValue JsonGetString(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonFieldArg(state, arg))
        return GameValue("");

    const GameArrayType& array = arg;
    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        return GameValue("");

    const cJSON* item = JsonField(root, GameStringToStdString(array[1]));
    GameValue result("");
    if (cJSON_IsString(item))
        result = item->valuestring ? item->valuestring : "";

    cJSON_Delete(root);
    return result;
}

GameValue JsonGetNumber(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonFieldArg(state, arg))
        return GameValue(0.0f);

    const GameArrayType& array = arg;
    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        return GameValue(0.0f);

    const cJSON* item = JsonField(root, GameStringToStdString(array[1]));
    GameValue result(0.0f);
    if (cJSON_IsNumber(item))
        result = static_cast<float>(item->valuedouble);

    cJSON_Delete(root);
    return result;
}

GameValue JsonGetBool(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonFieldArg(state, arg))
        return GameValue(false);

    const GameArrayType& array = arg;
    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        return GameValue(false);

    const cJSON* item = JsonField(root, GameStringToStdString(array[1]));
    GameValue result(false);
    if (cJSON_IsBool(item))
        result = cJSON_IsTrue(item) != 0;

    cJSON_Delete(root);
    return result;
}

GameValue JsonGetArray(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonFieldArg(state, arg))
        return EmptyGameArray(state);

    const GameArrayType& array = arg;
    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        return EmptyGameArray(state);

    const cJSON* item = JsonField(root, GameStringToStdString(array[1]));
    GameValue result = JsonArrayToGameValue(state, item);
    cJSON_Delete(root);
    return result;
}

GameValue JsonGet(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonFieldArg(state, arg))
        return GameValue();

    const GameArrayType& array = arg;
    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        return GameValue();

    const cJSON* item = JsonField(root, GameStringToStdString(array[1]));
    GameValue result = JsonValueToGameValue(state, item);
    cJSON_Delete(root);
    return result;
}

GameValue JsonSelect(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return EmptyGameArray(state);
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return EmptyGameArray(state);
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return EmptyGameArray(state);
    }
    if (array[1].GetType() != GameArray)
    {
        state->TypeError(GameArray, array[1].GetType());
        return EmptyGameArray(state);
    }

    const GameArrayType& keys = array[1];
    for (int i = 0; i < keys.Size(); ++i)
    {
        if (keys[i].GetType() != GameString)
        {
            state->TypeError(GameString, keys[i].GetType());
            return EmptyGameArray(state);
        }
    }

    GameValue value = EmptyGameArray(state);
    GameArrayType& selected = value;

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return value;
    }

    for (int i = 0; i < keys.Size(); ++i)
    {
        selected.Add(JsonValueToGameValue(state, JsonField(root, GameStringToStdString(keys[i]))));
    }

    cJSON_Delete(root);
    return value;
}

GameValue JsonPathGet(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonPathArg(state, arg, 2))
        return GameValue();

    const GameArrayType& array = arg;
    const GameArrayType& path = array[1];
    if (path.Size() == 0)
        return GameValue();

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return GameValue();
    }

    cJSON* parent = JsonPathParent(root, path, false);
    GameValue result;
    if (parent)
        result = JsonValueToGameValue(state, JsonField(parent, GameStringToStdString(path[path.Size() - 1])));

    cJSON_Delete(root);
    return result;
}

GameValue JsonPathSet(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonPathArg(state, arg, 3))
        return GameValue("");

    const GameArrayType& array = arg;
    const GameArrayType& path = array[1];
    if (path.Size() == 0)
        return GameValue(GameStringToStdString(array[0]).c_str());

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        root = cJSON_CreateObject();
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!root)
        return GameValue("");

    cJSON* parent = JsonPathParent(root, path, true);
    if (!parent)
    {
        cJSON_Delete(root);
        return GameValue("");
    }

    const std::string key = GameStringToStdString(path[path.Size() - 1]);
    cJSON* value = GameValueToJson(array[2]);
    cJSON_DeleteItemFromObjectCaseSensitive(parent, key.c_str());
    if (!value || !cJSON_AddItemToObject(parent, key.c_str(), value))
    {
        cJSON_Delete(value);
        cJSON_Delete(root);
        return GameValue("");
    }

    const std::string result = JsonToCompactString(root);
    cJSON_Delete(root);
    return GameValue(result.c_str());
}

GameValue JsonPathRemove(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonPathArg(state, arg, 2))
        return GameValue("");

    const GameArrayType& array = arg;
    const GameArrayType& path = array[1];
    if (path.Size() == 0)
        return GameValue(GameStringToStdString(array[0]).c_str());

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return GameValue("{}");
    }

    cJSON* parent = JsonPathParent(root, path, false);
    if (parent)
        cJSON_DeleteItemFromObjectCaseSensitive(parent, GameStringToStdString(path[path.Size() - 1]).c_str());

    const std::string result = JsonToCompactString(root);
    cJSON_Delete(root);
    return GameValue(result.c_str());
}

GameValue JsonStringify(const GameState* /*state*/, GameValuePar arg)
{
    cJSON* json = GameValueToJson(arg);
    const std::string result = JsonToCompactString(json);
    cJSON_Delete(json);
    return GameValue(result.c_str());
}

GameValue JsonObject(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return GameValue("");
    }

    cJSON* object = cJSON_CreateObject();
    if (!object)
        return GameValue("");

    const GameArrayType& pairs = arg;
    for (int i = 0; i < pairs.Size(); ++i)
    {
        if (pairs[i].GetType() != GameArray)
        {
            state->TypeError(GameArray, pairs[i].GetType());
            cJSON_Delete(object);
            return GameValue("");
        }

        const GameArrayType& pair = pairs[i];
        if (pair.Size() != 2)
        {
            state->SetError(EvalDim, pair.Size(), 2);
            cJSON_Delete(object);
            return GameValue("");
        }
        if (pair[0].GetType() != GameString)
        {
            state->TypeError(GameString, pair[0].GetType());
            cJSON_Delete(object);
            return GameValue("");
        }

        cJSON* value = GameValueToJson(pair[1]);
        if (!value || !cJSON_AddItemToObject(object, GameStringToStdString(pair[0]).c_str(), value))
        {
            cJSON_Delete(value);
            cJSON_Delete(object);
            return GameValue("");
        }
    }

    const std::string result = JsonToCompactString(object);
    cJSON_Delete(object);
    return GameValue(result.c_str());
}

GameValue JsonSet(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return GameValue("");
    }

    const GameArrayType& array = arg;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return GameValue("");
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return GameValue("");
    }
    if (array[1].GetType() != GameString)
    {
        state->TypeError(GameString, array[1].GetType());
        return GameValue("");
    }

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        root = cJSON_CreateObject();
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!root)
        return GameValue("");

    const std::string key = GameStringToStdString(array[1]);
    cJSON* value = GameValueToJson(array[2]);
    cJSON_DeleteItemFromObjectCaseSensitive(root, key.c_str());
    if (!value || !cJSON_AddItemToObject(root, key.c_str(), value))
    {
        cJSON_Delete(value);
        cJSON_Delete(root);
        return GameValue("");
    }

    const std::string result = JsonToCompactString(root);
    cJSON_Delete(root);
    return GameValue(result.c_str());
}

GameValue JsonRemove(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonFieldArg(state, arg))
        return GameValue("");

    const GameArrayType& array = arg;
    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        root = cJSON_CreateObject();
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    if (!root)
        return GameValue("");

    cJSON_DeleteItemFromObjectCaseSensitive(root, GameStringToStdString(array[1]).c_str());

    const std::string result = JsonToCompactString(root);
    cJSON_Delete(root);
    return GameValue(result.c_str());
}

GameValue JsonHas(const GameState* state, GameValuePar arg)
{
    if (!CheckJsonFieldArg(state, arg))
        return GameValue(false);

    const GameArrayType& array = arg;
    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return GameValue(false);
    }

    const cJSON* item = JsonField(root, GameStringToStdString(array[1]));
    const bool result = item != nullptr;
    cJSON_Delete(root);
    return GameValue(result);
}

GameValue JsonKeys(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
    {
        state->TypeError(GameString, arg.GetType());
        return EmptyGameArray(state);
    }

    GameValue value = EmptyGameArray(state);
    GameArrayType& keys = value;

    cJSON* root = cJSON_Parse(GameStringToStdString(arg).c_str());
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return value;
    }

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        keys.Add(GameValue(item->string ? item->string : ""));
    }

    cJSON_Delete(root);
    return value;
}

GameValue JsonValues(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
    {
        state->TypeError(GameString, arg.GetType());
        return EmptyGameArray(state);
    }

    GameValue value = EmptyGameArray(state);
    GameArrayType& values = value;

    cJSON* root = cJSON_Parse(GameStringToStdString(arg).c_str());
    if (!cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return value;
    }

    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root)
    {
        values.Add(JsonValueToGameValue(state, item));
    }

    cJSON_Delete(root);
    return value;
}

GameValue JsonCount(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameString)
    {
        state->TypeError(GameString, arg.GetType());
        return GameValue(0.0f);
    }

    cJSON* root = cJSON_Parse(GameStringToStdString(arg).c_str());
    const int count = cJSON_IsArray(root) ? cJSON_GetArraySize(root) : 0;
    cJSON_Delete(root);
    return GameValue(static_cast<float>(count));
}

GameValue JsonAt(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return GameValue();
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return GameValue();
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return GameValue();
    }
    if (array[1].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[1].GetType());
        return GameValue();
    }

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return GameValue();
    }

    const int index = toInt(static_cast<float>(array[1]));
    GameValue result = JsonValueToGameValue(state, cJSON_GetArrayItem(root, index));
    cJSON_Delete(root);
    return result;
}

GameValue JsonPush(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return GameValue("");
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return GameValue("");
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return GameValue("");
    }

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        root = cJSON_CreateArray();
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        root = cJSON_CreateArray();
    }
    if (!root)
        return GameValue("");

    cJSON* value = GameValueToJson(array[1]);
    if (!value)
    {
        cJSON_Delete(root);
        return GameValue("");
    }
    if (!cJSON_AddItemToArray(root, value))
    {
        cJSON_Delete(value);
        cJSON_Delete(root);
        return GameValue("");
    }

    const std::string result = JsonToCompactString(root);
    cJSON_Delete(root);
    return GameValue(result.c_str());
}

GameValue JsonInsert(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return GameValue("");
    }

    const GameArrayType& array = arg;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return GameValue("");
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return GameValue("");
    }
    if (array[1].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[1].GetType());
        return GameValue("");
    }

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!root)
        root = cJSON_CreateArray();
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        root = cJSON_CreateArray();
    }
    if (!root)
        return GameValue("");

    cJSON* value = GameValueToJson(array[2]);
    if (!value)
    {
        cJSON_Delete(root);
        return GameValue("");
    }

    const int index = toInt(static_cast<float>(array[1]));
    const int count = cJSON_GetArraySize(root);
    const bool added = index < 0 || index >= count ? cJSON_AddItemToArray(root, value) != 0
                                                   : cJSON_InsertItemInArray(root, index, value) != 0;
    if (!added)
    {
        cJSON_Delete(value);
        cJSON_Delete(root);
        return GameValue("");
    }

    const std::string result = JsonToCompactString(root);
    cJSON_Delete(root);
    return GameValue(result.c_str());
}

GameValue JsonSetAt(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return GameValue("");
    }

    const GameArrayType& array = arg;
    if (array.Size() != 3)
    {
        state->SetError(EvalDim, array.Size(), 3);
        return GameValue("");
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return GameValue("");
    }
    if (array[1].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[1].GetType());
        return GameValue("");
    }

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return GameValue("[]");
    }

    const int index = toInt(static_cast<float>(array[1]));
    if (index >= 0 && index < cJSON_GetArraySize(root))
    {
        cJSON* value = GameValueToJson(array[2]);
        if (!value || !cJSON_ReplaceItemInArray(root, index, value))
        {
            cJSON_Delete(value);
            cJSON_Delete(root);
            return GameValue("");
        }
    }

    const std::string result = JsonToCompactString(root);
    cJSON_Delete(root);
    return GameValue(result.c_str());
}

GameValue JsonDeleteAt(const GameState* state, GameValuePar arg)
{
    if (arg.GetType() != GameArray)
    {
        state->TypeError(GameArray, arg.GetType());
        return GameValue("");
    }

    const GameArrayType& array = arg;
    if (array.Size() != 2)
    {
        state->SetError(EvalDim, array.Size(), 2);
        return GameValue("");
    }
    if (array[0].GetType() != GameString)
    {
        state->TypeError(GameString, array[0].GetType());
        return GameValue("");
    }
    if (array[1].GetType() != GameScalar)
    {
        state->TypeError(GameScalar, array[1].GetType());
        return GameValue("");
    }

    cJSON* root = cJSON_Parse(GameStringToStdString(array[0]).c_str());
    if (!cJSON_IsArray(root))
    {
        cJSON_Delete(root);
        return GameValue("[]");
    }

    const int index = toInt(static_cast<float>(array[1]));
    if (index >= 0 && index < cJSON_GetArraySize(root))
        cJSON_DeleteItemFromArray(root, index);

    const std::string result = JsonToCompactString(root);
    cJSON_Delete(root);
    return GameValue(result.c_str());
}
