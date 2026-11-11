# Auto Layout

Builds a layout around the way **you** play, while you play.

1. Open a level in the editor and press the **GEN** button (top of the screen) so it says `GEN: ON`. The **Settings** button next to it opens the options.
2. Press playtest and just play.
   - Click in mid-air and a jump orb appears right where you are, so the click becomes an orb jump.
   - Every few seconds a gamemode, speed or mini portal appears in front of you.
   - Blocks and spikes are built right behind you, around the exact path you took.
3. Stop the playtest. Every portal gets walled in so it cannot be skipped.
4. `GEN` stays on until you turn it off. Each playtest with `GEN: ON` removes what was generated before (editor layer 77) and builds a new layout. Turn it off to test the result.

Every obstacle is checked against the path you actually played, so the level is always beatable the way you played it.

Settings: build while playing, gamemodes, frame perfects (60 / 120 / 240 fps), difficulty, density, orbs, speed and mini portals.

The idea of placing objects at the player during a playtest comes from ProfDragon's Layout Generator mod.
