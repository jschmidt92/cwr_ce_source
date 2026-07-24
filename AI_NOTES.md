# AI Notes

## Branch Synchronization

For this working fork, keep `main` updated from the CWR-CE branch that this
repository was forked from.

When updating feature branches, merge from this fork's `main` branch. Do not pull
directly from the original Bohemia Interactive source-release repository unless
the goal is to intentionally change the fork base.

## Code Structure

Follow the existing developer structure before adding new code. For scripting
commands, keep command declarations and registration entries in
`GameStateExt.cpp`, but place the implementation in the closest existing
`GameStateExt*` module:

- world/network/config commands: `GameStateExtWorldConfig.cpp`
- object commands: `GameStateExtObj.cpp`
- group commands: `GameStateExtGrp.cpp`
- UI/action commands: `GameStateExtUi.cpp`
- database, JSON, events, functions, and XEH commands: their dedicated
  `GameStateExt*.cpp` files

Avoid growing `GameStateExt.cpp` with feature implementation unless the existing
code already establishes that as the right home for the change.
