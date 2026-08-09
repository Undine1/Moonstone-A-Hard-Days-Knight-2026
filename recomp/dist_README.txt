================================================================
  MOONSTONE - A Hard Days Knight         native Windows port
================================================================

This is a prebuilt Windows package. The copyrighted original game
disks are not included; you must supply your own three ADF images.


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


---------------------------------------------------------------
 CONTROLS
---------------------------------------------------------------

  SKIP THE INTRO
    Press Space, Enter, or Ctrl on the keyboard, or A, B, LB, RB, or RT
    on a controller.

  GAME CONTROLLER (Xbox / generic XInput pad - recommended)
    Left stick / D-pad . move
    A / RB / LB / RT ... attack / select / confirm
    B .................. cancel / right-click
    Y ................... open inventory on the map
    Start ............... pause / resume walk and day animations
    Back ................. rest / end the current turn

  KEYBOARD + MOUSE
    Arrow keys ........... move
    Mouse ................. move the pointer
    Ctrl / Enter /
      Left-click ......... attack / select / confirm
    Space or I ........... open inventory on the map
    E ..................... rest / end the current turn
    Right-click .......... cancel / back
    Esc ................... quit

  NAME ENTRY
    Type a name with the keyboard. Backspace edits; Enter confirms.

  SAVE / LOAD
    F5 .................... quicksave anywhere, including combat
    F9 .................... quickload the last quicksave

    One save slot is written as moonstone.sav next to the game.

  SELECTION POPUPS
    Press Up for the first option and Down for the second.
    The number keys 1 and 2 also work.


---------------------------------------------------------------
 WHAT IS IN THIS FOLDER
---------------------------------------------------------------

  moonstone.exe          the native game
  SDL2.dll               window, input, and audio support
  README.txt             this guide
  LICENSE.txt            native runtime license
  THIRD-PARTY-NOTICES.txt  third-party licenses and credits
  data\                  place your three ADF images here

Do not rename or remove SDL2.dll or the data folder.


---------------------------------------------------------------
 TROUBLESHOOTING
---------------------------------------------------------------

If the game does not open, check that all three files are directly in
the data folder and named Disk1.adf, Disk2.adf, and Disk3.adf. Also
make sure SDL2.dll is still next to moonstone.exe.

Project page and native runtime source:
https://github.com/Undine1/Moonstone-A-Hard-Days-Knight-2026

MOONSTONE (c) 1991 Mindscape International / Rob Anderson.
Native runtime (c) 2026 Undine1, licensed under GPL v3.
