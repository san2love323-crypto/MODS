# Auto Layout

Builds a layout around the way **you** play, while you play.

1. Open a level in the editor and press the **GEN** button (top of the screen) so it says `GEN: ON`. The **Settings** button next to it opens the options.
2. Press playtest and just play. What you do decides what gets built:
   - Click in mid-air and an orb appears right where you are (orb chains). Higher difficulties add red, black and dash orbs.
   - After a dash, a wall of spikes is placed right behind the spot where you let go.
   - Some jumps end on a new platform that appears under you (stairs), and pads appear in front of your feet.
   - Fly level in ship / swing / UFO and you get a tight straight-fly tunnel. Spam in wave / UFO / ball and you get a spike-lined spam corridor.
   - Every few seconds a gamemode, speed, mini or gravity portal appears in front of you.
   - Blocks and spikes are built right behind you, around the exact path you took.
3. Stop the playtest. Every portal gets walled in so it cannot be skipped.
4. `GEN` stays on until you turn it off. Each playtest with `GEN: ON` removes what was generated before (editor layer 77) and builds a new layout. Turn it off to test the result.

Every obstacle is checked against the path you actually played, so the level is always beatable the way you played it.

Settings: build while playing, gamemodes, frame perfects (60 / 120 / 240 fps), difficulty (1-5 changes how tight and how advanced everything is), density, orbs, pads, platforms, gravity, speed and mini portals.

The idea of placing objects at the player during a playtest comes from ProfDragon's Layout Generator mod.
