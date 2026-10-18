# Path Explorer (raylib + C++17)

Walk only on the orange path. Camera is zoomed in; the map is revealed (fog of war) as you explore.

## Build / run
Windows: double-click `run.bat` (CMake + Ninja).   Or:
    cmake -B build
    cmake --build build --config Release

## Controls
WASD / Arrows = move | Shift = run | Mouse wheel = zoom
O / Space / Left-Click = swing attack
[  ]  (or - =) = vision radius smaller / bigger (hold Ctrl = 4x faster)
F1 = show walkable mask (green = path, magenta = path behind roofs)
F2 = minimap fog mode: FULL (black) -> DIM (dark but visible) -> OFF (no fog at all)
F3 = minimap | F4 = hide HUD | M = mute/unmute music
F5 / F6 (or 9 / 0) = BGM volume down / up (or click [-] / [+] / slider in HUD)

## What changed in this version
* **Wildlife NPC Critters System**:
  - Roaming in the meadows strictly **outside of town** and **only in the green grass areas** (never in town, never on building roofs, and never on the walkable roads).
  - Four distinct species with multi-directional sprite animations from `critters/`: **Stag (Deer)**, **Wild Boar**, **Badger**, and **Wolf** (with graceful procedural fallbacks).
  - **Full Animated Behaviors**:
    - **Sleeping**: Curled up comfortably resting on the turf with gentle sinusoidal breathing rise-and-fall pulsation and soft floating "zZz" particles drifting into the air.
    - **Walking**: Wandering through open meadows with natural leg step cycles and occasional grazing/sniffing pauses.
    - **Running**: Natural energetic sprints and playful dashes across open meadows between feeding or resting spots (peaceful ambient roaming, never fleeing from the player).
  - **Minimap Indicators**: Wildlife locations are subtly displayed on the minimap as ambient radar dots.
* **Sword Removed From Player**: The character sprite is clean during normal movement and idle—no sword is permanently drawn on or held in the player's hand.
* **Swinging Sprite (`swinging.png`) on `O`**: When pressing **`O`** (or **`Space`** / **`Left-Click`**), the newly added `GAMEEE/swinging.png` 4-directional sprite sheet plays smoothly (Row 0 = Down, Row 1 = Left, Row 2 = Right, Row 3 = Up) accompanied by `sword_swing.mp3`.
* **Main Character Size**: 50px tall (`AVATAR_HEIGHT`), making the character clearly visible, detailed, and well-proportioned while keeping path navigation and roof occlusion smooth.
* **Background Music (`origin_bg.mp3`) Volume**: Decreased default volume to a comfortable 20% background level. Added an interactive in-menu HUD volume bar with clickable `[-]`/`[+]` buttons and direct keyboard shortcuts (`F5`/`F6` or `9`/`0` and `M` for mute).
* **Walking & Running Footstep Sound Effects**: Integrated `GAMEEE/walking + running.mp3`, dynamically triggering rhythmic footsteps when walking and accelerating pitch and cadence when sprinting with `Shift`.
* **Sprite Animations**: Uses `down.png`, `up.png`, and `left_right.png` with 4-directional walk cycles, standing idle poses, and trilinear mipmap filtering.
* **Minimap Fog**: Fog is removed from the main game view and rendered dynamically on the minimap as areas are explored.


## New in this update
* **Time Stop - `T`**: freezes everything near you (enemies, wildlife) for **5 seconds** inside a glowing blue zone that follows you. You can still move and attack. 15 s cooldown (starts counting when you cast).
* **Shadow Step - `Q` or Right-Click**: throws a shadow orb toward the mouse cursor (max 300 px); it stops at the last walkable spot before a wall, and you teleport to where it lands. 3 s cooldown.
* **Floor 1**: goblins replaced with slimes (the last one at the Map 2 portal is a tougher **King Slime**).
* **Floor 4 (`assets/map4.png`, The Pentagram Arena)**: reach it through the red portal at the east end of the Cyclops arena on Map 2 (press `E` or step in). Walk down the south corridor to return. Holds a Stone Golem and slimes.
* **Store**: the player is drawn 2.2x bigger inside the shop (`g_avatarScale` in `src/main.cpp`).

## Tuning (top of src/main.cpp)
PathColorRule / RoofColorRule (colour detection), ROOF_MAX_RUN, ROOF_MARGIN,
WALK_SPEED, RUN_SPEED, AVATAR_HEIGHT, RADIUS_START, FOG_DIM_ALPHA, SPAWN_FX/FY.
