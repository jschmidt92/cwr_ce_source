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
```

`dbSave` writes immediately to disk. `dbLoad` reads the record from disk and
returns the stored JSON string.

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

## Cache Commands

```sqf
cacheLoad ["database", "key"]
cacheGet ["database", "key"]
cacheSet ["database", "key", json]
cacheFlush ["database", "key"]
cacheFlushAll
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
