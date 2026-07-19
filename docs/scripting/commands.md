# Scripting Commands

This page documents small generic scripting command additions that do not need a
dedicated topic page.

## Unit Loadout

```sqf
getUnitLoadout unit
unit setUnitLoadout loadout
```

`getUnitLoadout` captures:

- weapons
- magazines with ammo counts
- selected weapon or muzzle

Example:

```sqf
loadout = getUnitLoadout player
player setUnitLoadout loadout
```

The returned structure is suitable for JSON storage:

```sqf
state = jsonObject [
  ["position", getPosASL player],
  ["direction", getDir player],
  ["loadout", getUnitLoadout player]
]

dbSave ["actor", uid, state]
```

In SQS-style scripts, keep the `jsonObject` call on one line:

```sqf
state = jsonObject [["position", getPosASL player], ["direction", getDir player], ["loadout", getUnitLoadout player]]
dbSave ["actor", uid, state]
```

## Unit Life State

```sqf
lifeState unit
```

Returns `"HEALTHY"`, `"INJURED"`, `"UNCONSCIOUS"`, or `"DEAD"`.

## Script Events

See [event-system.md](event-system.md) for `eventOn`, `eventGet`, `eventList`,
`eventOff`, `eventClear`, and the domain-specific event emitters.

## Mission Lifecycle

See [mission-lifecycle.md](mission-lifecycle.md) for `missionPhaseOn`,
`missionPhaseOff`, `missionPhaseClear`, and `missionPhaseList`.
