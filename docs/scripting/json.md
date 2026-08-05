# JSON Commands

Poseidon exposes JSON helpers for creating, reading, updating, and querying JSON
documents from scripts. JSON values are represented as strings at the script
level, and mutation commands return a new JSON string.

## Commands

```sqf
jsonValid json
jsonStringify value
jsonObject [[key, value], ...]

jsonGet [json, key]
jsonGetString [json, key]
jsonGetNumber [json, key]
jsonGetBool [json, key]
jsonGetArray [json, key]
jsonSelect [json, keys]

jsonSet [json, key, value]
jsonRemove [json, key]
jsonHas [json, key]
jsonKeys json
jsonValues json

jsonPathGet [json, path]
jsonPathSet [json, path, value]
jsonPathRemove [json, path]

jsonCount jsonArray
jsonAt [jsonArray, index]
jsonPush [jsonArray, value]
jsonInsert [jsonArray, index, value]
jsonSetAt [jsonArray, index, value]
jsonDeleteAt [jsonArray, index]
```

## Objects

`jsonObject` creates a JSON object from key-value pairs.

```sqf
state = jsonObject [
  ["cash", 0],
  ["bank", 2000]
]
```

SQS-style scripts are parsed line by line, so keep the whole expression on one
line when using labels, `goto`, `exit`, or waits:

```sqf
state = jsonObject [["cash", 0], ["bank", 2000]]
```

`jsonSet` returns a new JSON string with the field changed:

```sqf
state = jsonSet [state, "cash", 100]
cash = jsonGet [state, "cash"]
```

`jsonRemove` returns a new JSON string without the field:

```sqf
state = jsonRemove [state, "cash"]
```

## Reading Values

Use `jsonGet` when the field may be a string, number, bool, or array:

```sqf
cash = jsonGet [state, "cash"]
loadout = jsonGet [state, "loadout"]
```

Use typed getters when the expected type should be explicit:

```sqf
uid = jsonGetString [state, "uid"]
cash = jsonGetNumber [state, "cash"]
alive = jsonGetBool [state, "alive"]
position = jsonGetArray [state, "position"]
```

Use `jsonSelect` to read several fields at once:

```sqf
values = jsonSelect [state, ["position", "direction", "loadout", "cash", "bank"]]

position = values select 0
direction = values select 1
loadout = values select 2
cash = values select 3
bank = values select 4
```

## JSON Path

`jsonPathGet`, `jsonPathSet`, and `jsonPathRemove` operate on nested object keys.

Example:

```sqf
world = dbLoad ["world", "state"]

cash = jsonPathGet [world, ["actors", uid, "cash"]]
world = jsonPathSet [world, ["actors", uid, "cash"], cash + 100]

dbSave ["world", "state", world]
```

This writes the whole `world/state.json` document back to disk. The path helper
only changes the nested value inside the JSON string returned to the script.

## Arrays

Use the array helpers when a JSON value is itself an array:

```sqf
items = jsonStringify ["M16", "Binocular"]
items = jsonPush [items, "HandGrenade"]
count = jsonCount items
first = jsonAt [items, 0]
```

`jsonInsert` inserts at the requested index and appends when the index is out of
range:

```sqf
items = jsonInsert [items, 1, "NVGoggles"]
```

`jsonSetAt` replaces an existing index and leaves the array unchanged when the
index is out of range:

```sqf
items = jsonSetAt [items, 0, "AK74"]
```
