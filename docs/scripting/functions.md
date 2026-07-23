# Script Functions

Poseidon can register named script functions into the game state so mission and
mod scripts can execute them with the native `call` operator.

## Commands

```sqf
functionRegister ["TAG_fnc_name", { statement; statement }]
functionRegister ["TAG_fnc_name", "path\fn_name.sqf"]
functionRegisterAddon ["TAG_fnc_name", { statement; statement }]
functionRegisterAddon ["TAG_fnc_name", "path\fn_name.sqf"]
functionExists "TAG_fnc_name"
functionGet "TAG_fnc_name"
functionUnregister "TAG_fnc_name"
functionUnregisterAddon "TAG_fnc_name"
functionList
functionClear
functionClearAddon
args spawn code
scriptDone scriptId
terminate scriptId
```

`functionRegister` registers a mission-scoped function and returns `true` when
the function is registered. `functionRegisterAddon` registers an addon-scoped
function. Code-backed functions are stored directly. File-backed functions load
the file, store its contents as code, and assign the function name in the script
VM.

After registration, use `call`:

```sqf
functionRegister ["IDS_fnc_addCash", {
  _unit = _this select 0
  _amount = _this select 1
  _amount
}]

[player, 500] call IDS_fnc_addCash
```

File-backed function:

```sqf
functionRegister ["IDS_fnc_loadActor", "functions\actors\fn_loadActor.sqf"]
[player] call IDS_fnc_loadActor
```

`spawn` schedules code on the world script runner and returns a numeric script
id:

```sqf
handle = [player] spawn IDS_fnc_trackActor
```

Use `scriptDone` to test whether a spawned script has finished. Unknown or
already removed script ids are treated as done. Use `terminate` to stop a
running spawned script; it returns `true` when an active script was found and
stopped.

```sqf
handle = ["hello"] spawn IDS_fnc_worker
done = scriptDone handle
stopped = terminate handle
```

`functionGet` returns:

```sqf
[name, lifetime, type, source, body]
```

`lifetime` is `"mission"` or `"addon"`. `type` is `"code"` or `"file"`.
`source` is empty for code-backed functions.

`functionList` returns all registered function records.

Mission functions override addon functions with the same name. If a mission
function is cleared or unregistered, the addon function underneath is rebound in
the VM automatically.

`functionUnregister` removes the mission-scoped function for a name.
`functionUnregisterAddon` removes the addon-scoped function for a name.
`functionClear` removes mission-scoped functions. `functionClearAddon` removes
addon-scoped functions.

Mission-scoped functions are cleared at mission init. Addon-scoped functions
remain registered across mission changes until the addon layer is explicitly
cleared or unregistered. Future config discovery can populate addon functions
from addon configs and mission functions from `description.ext` before mission
scripts run.
