# ProxySkinVolume

Editor-only Unreal Engine 5.6.1 plugin for baking trimmed proxy skin meshes from geometry inside a box volume.

## Core workflow

1. Place `ProxySkinVolume` actor in level.
2. Set `VolumeBox` as collection/trim region.
3. Place `PivotSphere` as desired pivot.
4. Click **Bake Proxy Skin**.

## Notes

- Bake runs only manually (no auto rebuild on move/scale/property edits).
- Result actor is spawned hidden in game.
