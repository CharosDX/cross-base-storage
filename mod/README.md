# Cross-Base Storage

The magic chests of all your bases work together.
Stone in one base and wood in another? Build and craft with both, in every base, without carrying anything around.

## What it does

- **Building:** the build menu shows the materials from the magic chests of all your bases, and placing blocks takes them from there.
- **Crafting:** workbenches and the crafting menu use the magic chests of all your bases.
- **Station queues:** the +1 and +10 buttons of production stations fill the queue from the magic chests of all your bases.

Materials come from your inventory first, then from the magic chests. The base you are in always comes first, other bases only fill up what is missing.

## What counts

Only magic chests count from other bases. Normal chests never count as crafting stock, just like in the base game.
Using my [Craft From Stations](https://www.nexusmods.com/enshrouded/mods/215) mod? Its production stations only count as storage in the base they're placed in.

## Requirements

None. It's a small native DLL, EML is not needed. If you use EML, it simply skips this mod.

## Installation

1. Extract the archive into your Enshrouded folder (next to enshrouded.exe). You get `dinput8.dll` (small loader) and the `mods\cross-base-storage` folder.
2. Start the game.

Already using another mod with a dinput8.dll?

- The **Queue Fix** of my other mod [Craft From Stations](https://www.nexusmods.com/enshrouded/mods/215) uses the very same `dinput8.dll`. Overwriting it is fine, both mods keep working.
- Mods from other authors: don't overwrite their `dinput8.dll`. Install with the [Multi DLL Installer](https://www.nexusmods.com/enshrouded/mods/163) instead.

## Settings

On the first start the mod creates `mods\cross-base-storage\native\cross-base-storage.ini`. Restart the game after changing it.

- `inventory_first=1` (default): materials come from your inventory first, then from the magic chests.
- `inventory_first=0`: magic chests first, then your inventory, like the base game when building.

## Co-op

Everyone needs the mod, the host and every guest. Dedicated servers are not supported.
The host's `inventory_first` setting decides where materials come from. Guests should use the same setting, or their build menu may briefly show the wrong count.

## Troubleshooting

Materials from other bases don't show up?

1. Make sure the materials are in **magic** chests.
2. Open `mods\cross-base-storage\native\`. The loader writes `native-loader.log` there. If it's missing, the loader didn't run: make sure `dinput8.dll` is next to enshrouded.exe.
3. `cross-base-storage.log` in the same folder should say `mod active`. If it says `mod NOT active`, the game was updated and the mod needs an update too.
4. Please report it in the Bugs tab and attach both log files.

## Uninstall

1. Remove the `cross-base-storage` folder from `Enshrouded\mods\`.
2. Delete `dinput8.dll` from the Enshrouded folder, unless another mod uses it (for example the Queue Fix).

## Changelog

- **1.1.0:** Materials now come from your inventory first, then from the magic chests. Can be switched back in `cross-base-storage.ini`.
- **1.0.0:** Out of beta. Tested in single player and co-op, no gameplay changes.
- **0.9.1:** Safer memory handling and version details in the DLL files.
- **0.9.0:** First release (Beta).
