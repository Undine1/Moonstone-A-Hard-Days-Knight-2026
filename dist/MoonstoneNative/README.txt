================================================================
  MOONSTONE - A Hard Days Knight         native Windows port
================================================================

Moonstone 2026 v1.2.0 - prebuilt Windows package. ADF files are not included.


---------------------------------------------------------------
 HOW TO PLAY
---------------------------------------------------------------

1. Extract the complete ZIP.
2. Supply your own ADF files into the included data folder. The ADF files
   should come in a batch of three disk files and should be named specifically:

       Disk1.adf
       Disk2.adf
       Disk3.adf

3. Double-click:

       moonstone.exe

On first launch, the game extracts its required boot modules from
Disk1.adf automatically. Each ADF must be 901,120 bytes. Requires
64-bit Windows. Keep moonstone.exe, SDL2.dll, and the data folder
together. The folder is portable.

The game opens without a separate command window. A brief on-screen message
shows when a controller connects or disconnects, including one already
connected at startup. Diagnostic details are saved in moonstone.log.

UPDATING FROM AN EARLIER VERSION
Replace moonstone.exe and SDL2.dll in your existing game folder with the new
copies. Keep your data folder, moonstone.sav, and customized controls.ini.
The included controls.ini supplies defaults for a new installation.


---------------------------------------------------------------
 CONTROLS
---------------------------------------------------------------

  Edit controls.ini beside moonstone.exe and restart the game.
  Deleting it restores the defaults shown below.

  PRACTICE
    Control the blue knight. The green knight stays idle and reacts when hit.
    Separate controls for a second player are not currently supported.

  SKIP THE INTRO
    Press Space, Enter, or Ctrl on the keyboard, or A, B, LB, RB, or RT
    on a controller.

  GAME CONTROLLER (Xbox / generic XInput pad)
    Left stick / D-pad . move / move the pointer
    A / RB / LB / RT ... attack / select / confirm
    Y ................... open inventory on the map
    Start ............... pause / resume during combat
    Select / Back ........ rest / end the current turn (pass the day)

  KEYBOARD (no mouse required)
    Arrow keys ........... move / move the pointer
    Ctrl / Enter /
      Numpad Enter ....... attack / select / confirm
    Space or I ........... open inventory on the map
    Space (in combat) .... pause / resume
    E ..................... rest / end the current turn
    Q ..................... abandon the current quest / return to setup
    V ..................... show the game version on the map
    Esc ................... quit

  MOUSE (optional)
    Mouse movement ........ move the pointer
    Left-click ........... attack / select / confirm

  NAME ENTRY
    Type a name with the keyboard. Backspace edits; Enter or Numpad Enter confirms.

  SAVE / LOAD
    F5 .................... quicksave anywhere, including combat
    F9 .................... quickload the last quicksave

    One save slot is written as moonstone.sav next to the game.

  SELECTION POPUPS
    Press Up for the first option and Down for the second.
    The number keys 1 and 2 also work.


---------------------------------------------------------------
 TROUBLESHOOTING
---------------------------------------------------------------

If the game does not open, check that all three files are directly in
the data folder and named Disk1.adf, Disk2.adf, and Disk3.adf. Also
make sure SDL2.dll is still next to moonstone.exe.

If startup data or a requested save cannot be loaded, an error dialog names
the file. Details are written to moonstone.log (or the path given by --log).
Warnings also appear if sound, audio recording, or the log file is unavailable.

Project page and native runtime source:
https://github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026

MOONSTONE (c) 1991 Mindscape International / Rob Anderson.
Native runtime (c) 2026 Undine1, licensed under GPL v3.
