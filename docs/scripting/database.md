# Database and Cache

Poseidon exposes a small generic persistence layer to scripts. It is intentionally
not tied to a specific mod or game mode. Higher-level systems can build on these
commands for actors, accounts, world state, economy data, mission state, or editor
tools.

## Storage Layout

Database records are stored under the active profile directory:

```text
<profileDirectory>/LocalDb/<database>/<key>.json
```

For example:

```sqf
dbSave ["actor", "76561198027566824", state]
```

writes:

```text
LocalDb/actor/76561198027566824.json
```

And:

```sqf
dbSave ["world", "state", world]
```

writes:

```text
LocalDb/world/state.json
```

The `database` and `key` values are safe identifiers, not arbitrary paths. This
keeps scripts from escaping the database root.

## Database Commands

```sqf
dbRoot
dbSave ["database", "key", json]
dbLoad ["database", "key"]
dbRemove ["database", "key"]
dbExists ["database", "key"]
dbList "database"
dbFind ["database", "field", value]
dbFindPath ["database", ["nested", "field"], value]
dbIndex ["database", "field"]
dbIndexPath ["database", ["nested", "field"]]
dbUpdate ["database", "key", { jsonSet [_this, "cash", (jsonGet [_this, "cash"]) + 100] }]
```

`dbSave` writes immediately to disk. `dbLoad` reads the record from disk and
returns the stored JSON string. These commands are synchronous and should be
kept out of hot gameplay paths.

## Async Database Commands

```sqf
job = dbAsyncSave ["database", "key", json]
job = dbAsyncLoad ["database", "key"]
job = dbAsyncRemove ["database", "key"]
job = dbAsyncExists ["database", "key"]
job = dbAsyncList "database"
job = dbAsyncFind ["database", "field", value]
job = dbAsyncFindPath ["database", ["nested", "field"], value]
job = dbAsyncIndex ["database", "field"]
job = dbAsyncIndexPath ["database", ["nested", "field"]]
dbAsyncDone job
dbAsyncResult job
dbAsyncClear job
dbAsyncJobs
```

The async commands copy their arguments immediately, enqueue file work on the
local DB worker thread, and return a numeric job id. They do not wait for disk
I/O on the script thread. Invalid arguments or unavailable queued data return
`-1`. Completed async job records should be cleared with `dbAsyncClear`; the
engine prunes old completed jobs when the async job table reaches its safety cap.

`dbAsyncResult` returns an array:

```sqf
[jobId, status, ok, result]
```

`status` is `"queued"`, `"running"`, or `"done"`. `ok` is meaningful once the
status is `"done"`.

The result field matches the operation:

- `dbAsyncLoad`: stored JSON string, or `""`/nil on failure.
- `dbAsyncExists`: boolean.
- `dbAsyncList`, `dbAsyncFind`, `dbAsyncFindPath`: array of keys.
- `dbAsyncIndex`, `dbAsyncIndexPath`: array of `[fieldValue, [key, ...]]`.
- `dbAsyncSave`, `dbAsyncRemove`: no result payload; use `ok`.

Example:

```sqf
job = dbAsyncLoad ["actor", "76561198027566824"]

#poll
~0.1
? !(dbAsyncDone job) : goto "poll"

result = dbAsyncResult job
ok = result select 2
state = result select 3
dbAsyncClear job
```

`dbUpdate` remains synchronous because it executes a script code block in the VM
between load and save. Use async load/save with an explicit script-side update
flow when a non-blocking gameplay path is required.

Example:

```sqf
state = jsonObject [
  ["uid", "76561198027566824"],
  ["cash", 0],
  ["bank", 2000],
  ["position", getPosASL player],
  ["direction", getDir player],
  ["loadout", getUnitLoadout player]
]

dbSave ["actor", "76561198027566824", state]
```

In SQS-style scripts, keep the full `jsonObject` expression on one line:

```sqf
state = jsonObject [["uid", "76561198027566824"], ["cash", 0], ["bank", 2000], ["position", getPosASL player], ["direction", getDir player], ["loadout", getUnitLoadout player]]
dbSave ["actor", "76561198027566824", state]
```

## Query And Index Helpers

`dbFind` scans the keys from `dbList`, loads each JSON record, and returns the
keys whose top-level field matches the requested value.

```sqf
westActors = dbFind ["actor", "side", "WEST"]
richActors = dbFind ["actor", "cash", 1000]
```

`dbFindPath` does the same check against a nested object path:

```sqf
medics = dbFindPath ["actor", ["role", "class"], "medic"]
```

`dbIndex` groups records by a top-level JSON field and returns pairs of
`[fieldValue, [matchingKey, ...]]`.

```sqf
bySide = dbIndex ["actor", "side"]
```

`dbIndexPath` groups by a nested path:

```sqf
byTown = dbIndexPath ["container", ["location", "town"]]
```

These helpers are scan-based indexes. They are intended for moderate profile
databases and mission startup/catalog use. For hot gameplay paths, cache the
result in script memory or maintain a dedicated index record with `dbUpdate`.

## Atomic Updates

`dbUpdate` loads one record, runs a code block with `_this` set to the current
JSON string, and saves the string returned by the code block. The load, script
transform, save, and cache refresh run under the local DB command lock, so two
script commands in the same process cannot interleave a read-modify-write cycle.

```sqf
dbUpdate ["actor", uid, {
  state = _this
  cash = jsonGet [state, "cash"]
  jsonSet [state, "cash", cash + 100]
}]
```

SQS-safe inline form:

```sqf
dbUpdate ["actor", uid, { state = _this; cash = jsonGet [state, "cash"]; jsonSet [state, "cash", cash + 100] }]
```

If the record does not exist, `_this` is an empty string. The block should return
a complete JSON document.

## Cache Commands

```sqf
cacheLoad ["database", "key"]
cacheGet ["database", "key"]
cacheSet ["database", "key", json]
cacheFlush ["database", "key"]
cacheFlushAll
cacheAsyncFlush ["database", "key"]
cacheAsyncFlushAll
cacheRemove ["database", "key"]
cacheClear
```

The cache layer keeps records in memory so scripts can avoid repeated disk reads
and writes during gameplay.

- `cacheLoad` reads one record from disk into memory.
- `cacheGet` returns the in-memory JSON string.
- `cacheSet` updates only memory.
- `cacheFlush` writes one cached record to disk and keeps it in memory.
- `cacheFlushAll` writes all cached records for the active profile.
- `cacheAsyncFlush` and `cacheAsyncFlushAll` enqueue flush work on the local DB
  worker thread. They read the current cache when the worker runs, so later
  `cacheSet` calls can still affect the async flush before it starts.
- `cacheRemove` removes one record from memory only.
- `cacheClear` clears all cached records from memory.

Example: update cash in memory, then flush later.

```sqf
uid = "76561198027566824"

cacheLoad ["actor", uid]

state = cacheGet ["actor", uid]
cash = jsonGet [state, "cash"]
state = jsonSet [state, "cash", cash + 100]

cacheSet ["actor", uid, state]
cacheFlush ["actor", uid]
```

## Actor Save And Load

Save the current player:

```sqf
uid = _this select 0

state = jsonObject [
  ["uid", uid],
  ["cash", 0],
  ["bank", 2000],
  ["position", getPosASL player],
  ["direction", getDir player],
  ["loadout", getUnitLoadout player]
]

cacheSet ["actor", uid, state]
cacheFlush ["actor", uid]
```

SQS-safe version:

```sqf
uid = _this select 0
state = jsonObject [["uid", uid], ["cash", 0], ["bank", 2000], ["position", getPosASL player], ["direction", getDir player], ["loadout", getUnitLoadout player]]
cacheSet ["actor", uid, state]
cacheFlush ["actor", uid]
```

Load and apply the saved player:

```sqf
uid = _this select 0

loaded = cacheLoad ["actor", uid]

? !loaded : goto "new_actor"

state = cacheGet ["actor", uid]
values = jsonSelect [state, ["position", "direction", "loadout", "cash", "bank"]]

player setPosASL (values select 0)
player setDir (values select 1)
player setUnitLoadout (values select 2)

cash = values select 3
bank = values select 4

hint format ["Loaded actor\nCash: %1\nBank: %2", cash, bank]
exit

#new_actor
state = jsonObject [
  ["uid", uid],
  ["cash", 0],
  ["bank", 2000],
  ["position", getPosASL player],
  ["direction", getDir player],
  ["loadout", getUnitLoadout player]
]

cacheSet ["actor", uid, state]
cacheFlush ["actor", uid]

hint "Created new actor"
```

When this load script uses SQS labels and `goto`, write the new actor state on
one line:

```sqf
#new_actor
state = jsonObject [["uid", uid], ["cash", 0], ["bank", 2000], ["position", getPosASL player], ["direction", getDir player], ["loadout", getUnitLoadout player]]
cacheSet ["actor", uid, state]
cacheFlush ["actor", uid]
```

## Single Record Versus World Document

There are two useful persistence patterns.

Per-record files:

```text
LocalDb/actor/<uid>.json
LocalDb/vehicle/<id>.json
LocalDb/container/<id>.json
```

This is good when records are independent and can be loaded or flushed one at a
time.

One shared document:

```text
LocalDb/world/state.json
```

This is useful when scripts want one world state document with nested sections:

```json
{
  "actors": {},
  "vehicles": {},
  "economy": {},
  "mission": {}
}
```

Use `jsonPathGet`, `jsonPathSet`, and `jsonPathRemove` for this pattern. Saving
still writes the whole document.
