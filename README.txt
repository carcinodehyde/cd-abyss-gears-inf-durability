===============================================================================
  Abyss Durability  v1.0  -  Crimson Desert ASI mod
===============================================================================

Abyss Gear never runs out of uses.

The "_Special" Abyss Gear (for example the 100% "don't consume ammo" gear) is
limited to 100 uses. This mod lifts that limit by giving those items the same
unlimited durability that normal, non-wearing items have.

Runtime only: the change is made in memory while the game loads its item
table. No game files are modified, and no mod manager is needed.


-------------------------------------------------------------------------------
  AFFECTED ITEMS (14)
-------------------------------------------------------------------------------

  Item_Stat_AbyssGear_DDD_Special
  Item_Stat_AbyssGear_CriticalRate_Special
  Item_Stat_AbyssGear_AttackSpeedRate_Special
  Item_Stat_AbyssGear_FireResistance_Special
  Item_Stat_AbyssGear_IceResistance_Special
  Item_Stat_AbyssGear_ElectricityResistance_Special
  Item_Skill_AbyssGear_Machine_AdditionalDamage_Special
  Item_Skill_AbyssGear_CommonHexe_AdditionalDamage_Special
  Item_Skill_AbyssGear_CommonHumanoid_AdditionalDamage_Special
  Item_Skill_AbyssGear_CommonGolem_AdditionalDamage_Special
  Item_Skill_AbyssGear_Boss_AdditionalDamage_Special
  Item_Skill_AbyssGear_CommonAnimal_AdditionalDamage_Special
  Item_Skill_AbyssGear_Creature_AdditionalDamage_Special
  Item_Skill_AbyssGear_BlockAmmoConsume_Special

The mod finds these by name pattern (Item_*AbyssGear* with limited durability),
so Abyss Gear added in a later patch is covered too if the table layout stays
the same. Nothing else is touched.


-------------------------------------------------------------------------------
  REQUIREMENTS
-------------------------------------------------------------------------------

  - An ASI loader, e.g. Ultimate ASI Loader (winmm.dll or dinput8.dll in
    Crimson Desert\bin64).
  - Works with or without the DLC (all affected items are in the base game).


-------------------------------------------------------------------------------
  INSTALL
-------------------------------------------------------------------------------

  Copy these two files into:   <Steam>\steamapps\common\Crimson Desert\bin64\

      AbyssDurability.asi
      AbyssDurability.ini

  Launch the game. AbyssDurability.log appears next to the .asi.


-------------------------------------------------------------------------------
  UNINSTALL
-------------------------------------------------------------------------------

  Delete AbyssDurability.asi (and .ini / .log), or set Enabled=0 in the .ini.
  Nothing on disk was changed, so the game is back to vanilla immediately.


-------------------------------------------------------------------------------
  CONFIG  (AbyssDurability.ini)
-------------------------------------------------------------------------------

  [AbyssDurability]
  Enabled=1        ; 1 = unlimited Abyss Gear uses, 0 = vanilla


-------------------------------------------------------------------------------
  CHECKING IT WORKS  (AbyssDurability.log)
-------------------------------------------------------------------------------

  Good:
    file reader hooked at +0x14AA7C0
    item table (5804162 bytes): 14 Abyss Gear item(s) made unbreakable, ...

  "(another plugin had hooked it first; chained onto its hook)"
    Normal when another ASI mod (e.g. Pack Rat) hooks the same function.
    Both mods keep working.

  "ERROR: the game's file reader was not found"
    The game was updated and the mod needs a new version. Nothing was changed;
    the game runs as vanilla.

  Hook line present but no "item table" line
    The item table's layout changed in a game update. The mod refuses to edit
    a table it doesn't recognise, so nothing was changed.


-------------------------------------------------------------------------------
  COMPATIBILITY
-------------------------------------------------------------------------------

  - Built and tested against the 30 Oct 2026 game update.
  - Works on its own, without any other ASI mods (tested).
  - Pack Rat (Fyreon87): compatible, both hook the game's file reader and
    chain onto each other. Tested with Pack Rat loading first.
  - Mods that replace iteminfo (file replacers / DMM JSON mods): fine, this
    mod edits whatever item table the game ends up loading.
  - Abyss Gear you already own keeps working; tested on an existing save.


-------------------------------------------------------------------------------
  HOW IT WORKS
-------------------------------------------------------------------------------

  The game reads its data tables through one resource-reader function. The
  mod finds it by byte signature, hooks it, and when the item table
  (iteminfo.staticinfobody) is read, sets max_endurance of the limited Abyss
  Gear records from 100 to 65535 - the value every non-wearing item uses.

  The table is recognised by its contents, not its file name. Record layout:
    u32 nameLength, name, u32 key, u8 0, u16 max_endurance,
    u64 max_stack_count, 01 02 ...

  Source code is included in src\abyss_durability.c.


-------------------------------------------------------------------------------
  BUILDING FROM SOURCE
-------------------------------------------------------------------------------

  zig cc -target x86_64-windows-gnu -O2 -shared -o AbyssDurability.asi src\abyss_durability.c

  (Any MinGW-w64 gcc works too: x86_64-w64-mingw32-gcc -O2 -shared ...)


-------------------------------------------------------------------------------
  CREDITS
-------------------------------------------------------------------------------

  - Fyreon87's Pack Rat, for showing that the game's file reader is the right
    place to edit data tables at runtime.
  - Lazorr410's Crimson Desert Unpacker, used to extract the tables for
    research.
