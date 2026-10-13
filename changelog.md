v1.0.0
Initial release
Switches to a random StartPos after you die
Setting for how many deaths before it switches
On/off toggle
v1.1.0
Switch modes: random, next, previous
Randomized death count
Avoid repeats, excluded StartPos list, include level start
Popup and sound when the StartPos changes
On-screen label
Pause menu buttons
v1.2.0
More reliable auto-switching
New mode: ping-pong
Switch on level complete
Sound choice and popup duration
Label color, size and total death counter
v1.3.0
New pick modes: shuffle and weighted
New triggers: chance and streak
Presets: practice, challenge, chaos, drill
Compact pause panel
Settings grouped into sections
v1.4.0
Default deaths until switch is 3
Mod off means no processing
Switch indicator ("Next: StartPos") as you die
Practice styles: beat to advance, build-up runs
Seeded sequences
StartPos stats with a button on the level page
In-game tabbed menu with info buttons and 5 themes
Compatibility warning
Debug log option
(A respawn delay was added here and removed again in v1.6)
v1.5.0
In-game menu rebuilt with 9 categories
Menu style option (solid, glass, outline)
Main menu button
Version number shown in the menu
v1.6.0
Every death is now counted, always (the "only count after %" and "disable in practice mode" options are gone)
Death detection no longer depends on the game's dead flag, so switching keeps working after the first switch and when you are not playing from 0%
Better StartPos finding: three independent sources are merged, and every StartPos is kept alive so the mod can never point at a StartPos the game threw away
The label now says "none found" if a level's StartPos can't be found, so you can see what the mod sees
The classic on-screen label is back (plain text), replacing the pill label
Safe mode reworked: any run that starts from a StartPos (the mod's or another mod's) is protected, progress is never saved, and a new best is not recognised
Entering a level warns you if Smart StartPos or StartPos Switcher may be on, because they are incompatible
The pause panel's gear is replaced by a "Menu" button that opens the new in-game menu
Main menu "SP" button now works even when the bottom bar can't be found
Removed: respawn delay and freeze, hold music, clear checkpoints (checkpoints are never touched)
Fewer things run per death and per frame
