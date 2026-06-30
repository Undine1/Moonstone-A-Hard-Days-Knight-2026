================================================================
  MOONSTONE  -  A Hard Days Knight        (native Windows port)
================================================================

This is the original 1991 Amiga game MOONSTONE, running NATIVELY on
Windows.  There is NO emulator (no WinUAE / FS-UAE).  The real Amiga
68000 game code is executed directly by an embedded CPU core, on top
of a from-scratch model of the Amiga's custom chips (graphics, the
blitter, the copper, and 4-channel Paula audio).

The three original floppy disks are bundled as data, and the game
swaps between them AUTOMATICALLY - you will never be asked to "insert
Disk B / Disk C".


---------------------------------------------------------------
 HOW TO RUN
---------------------------------------------------------------

Just double-click:

    moonstone.exe          (or  "Play Moonstone.cmd")

A window opens and the game boots straight to the MOONSTONE title
screen and main menu.  Everything it needs is in the "data" folder
next to the exe - you can move or copy the whole MoonstoneNative
folder anywhere.

Requires: 64-bit Windows.  SDL2.dll (included) must stay next to the
exe.


---------------------------------------------------------------
 CONTROLS
---------------------------------------------------------------

  GAME CONTROLLER (Xbox / generic XInput pad - recommended)
    Left stick / D-pad . move (menu cursor, overland knight, combat knight)
    A / RB / LB / RT ... attack / select / confirm / "press fire"
    B .................. cancel / right-click
    Y / Start ......... open INVENTORY (on the overland map)
    Back .............. REST / skip to the next turn (on the overland map)
    (plug-and-play; hot-plug supported; quit is Esc or the window close box)

  NAME ENTRY (Select Knight)
    Type your knight's name on the keyboard (A-Z, 0-9, space);
    Backspace edits, Enter/fire confirms.

  KEYBOARD + MOUSE
    Arrow keys ........ move (menu cursor / knight)
    Mouse ............. also moves the cursor (used on some screens)
    Ctrl / Enter /
      Left-click ...... attack / select / confirm / "press fire"
    Space  (or I) ..... open INVENTORY (on the overland map) -- the Amiga control
    E ................. REST / skip to the next turn (overland map) -- the Amiga control
    Right-click ....... cancel / back
    Esc ............... quit

  SAVE / LOAD  (this port adds saving -- the 1991 original had none)
    F5 ............... QUICKSAVE  (save your progress anywhere, even mid-fight)
    F9 ............... QUICKLOAD  (restore your last quicksave)
    (one save slot, written as 'moonstone.sav' next to the game; a brief
     "GAME SAVED" / "GAME LOADED" appears in the window title bar)

Notes:
  * Both menus and combat are driven by the same stick/d-pad/arrows, so
    one controller (or the keyboard) works everywhere.
  * SELECTION POPUP (when entering a spot offers two choices, e.g. a
    wilderness zone vs. the neighbouring city): push UP for the first
    option, DOWN for the second (or press the 1 / 2 keys).
  * Some screens (the equip/altar screen, the overland map) are
    cursor-based - the stick / mouse moves a pointer; press fire to act.

Tip: choose "Practice" from the main menu to jump straight into a
combat arena with a controllable knight.


---------------------------------------------------------------
 WHAT'S IN THIS FOLDER
---------------------------------------------------------------

  moonstone.exe          the native game
  SDL2.dll               window / input / sound library (keep next to exe)
  Play Moonstone.cmd      convenience launcher
  data\                  the original game data:
      nb, program, mog, crystal           game boot modules
      Moonstone ... Disk1/2/3.adf         the three original floppy images

The data is the original Mindscape disk content; this port only adds
the native runtime around it.


---------------------------------------------------------------
 ADVANCED / TROUBLESHOOTING
---------------------------------------------------------------

The exe finds "data\" relative to its own location.  If you keep the
data elsewhere you can point at it:

    moonstone.exe --dataset PATH\to\data --diskdir PATH\to\data

Other options:
    --scale N        window scale (default 3)
    --hardpan        faithful Amiga hard L/R stereo (default blends the
                     channels slightly for a more natural sound)
    --audioblend N   stereo crossfeed 0-50 (0 = hard-pan, 50 = mono; default 35)
    --noautoswap     turn OFF automatic disk swapping (you will then be
                     prompted to swap disks, as on a real Amiga)

If the window does not open, make sure SDL2.dll is in the same folder
as moonstone.exe.

  MOONSTONE (c) 1991 Mindscape International / Rob Anderson.
  Native runtime: original game code under an embedded 68000 core +
  custom-chip model.  No Amiga ROM is required or included.
