# LiD Login Bonus

Brings the daily login bonus back to the offline version of Let It Die
(Steam). Each new game day, the first time you reach the hub, a random reward
lands in your Rewards Box, picked from the rewards the online game's login
calendar gave out.

- **lid-loginbonus.dll** (`dll/`): a plugin for the
  [LiD Plugin Loader](https://github.com/NicoMullin/LiD-Plugin-Loader).
- **Login Bonus** ([mods/Login Bonus](mods/Login%20Bonus)): an optional mod for
  the LET IT DIE DB Mod Manager (0.12.5 or newer) that chooses what the bonus
  draws from: the online lists, or your own, edited in a window.

## What you get

One reward a day, every reward on the list with the same chance. With the
Express Pass active, you get a second one from the same list.

The list depends on how far up the Tower of Barbs you have been, like the
online game's:

| Floor tier | Rewards from (`master_login_bonus`) | You are on it when                   |
|------------|-------------------------------------|--------------------------------------|
| 1-20F      | `DATE` (65 rewards)                 | always                               |
| 21-40F     | `DATE_O21` (64 rewards)             | the 21F or 31F elevator stop is open |
| 41F and up | `DATE_O41` (62 rewards)             | the 41F or 51F elevator stop is open |

The lists are every reward the online calendar ever gave at that tier that
arrives as **one present**, each once: Kill Coins, SPLithium, Death Metals,
Express Pass days, and single materials, healing items, mushrooms and Lost
Bags. The game makes one present per item, so a reward of several items (such
as 4 rare metals, 5 mushrooms or 7 beasts) would fill the Rewards Box with
that many presents; those are left out. Amounts are always one present. The
calendar sometimes gave the very same thing under several reward ids (20,000
Kill Coins under four); those count once, so nothing comes up more often than
the rest.

The rewards themselves come from `master_reward`, so mods that change a reward
(such as Death Metals, which turns the `RWD_MEDAL` rewards into Death Metals)
change it here too.

The day turns at 10:00 UTC, the same as everything else in the game. The pick
is random but tied to your save's login count, which the game raises by one
each new day: quitting without saving and coming back gives the same reward,
not a new roll, while every day the game counts gives a new one (including a
day started early with the mod manager's **Start a new game day**).

### A list of your own

The **Login Bonus** mod in [mods/Login Bonus](mods/Login%20Bonus) does this for
you: in the mod manager, set **Bonus lists** to **Your own lists** and edit a
list for each floor tier, with a weight for each reward.

Underneath, a tier's own list is rows in `master_login_bonus` with
`cond_type` `LID_RANDOM`, `LID_RANDOM_O21` or `LID_RANDOM_O41`, the reward in
`rwdid` and its weight in `cond_days` (a reward with weight 3 comes up three
times as often as one with weight 1). When a tier has rows like that, only they
are used. The online game never read these, and the offline game reads none of
the table, so they change nothing else. A mod's own list is used as it is,
so a reward of several items in it does arrive as several presents.

## How it works

The online game's server picked each day's bonus from its calendar. The
offline game never reads that calendar, but two pieces survived:

- When you reach the hub, the game's built-in server runs a once-a-day check.
  It records the new game day and answers yes exactly once per day (the same
  yes restocks Hernia's vending machine).
- The function the server used to turn a reward id into a Rewards Box present
  is still in the game.

The plugin hooks the daily check. The game's own check always runs first, and
only on its yes is a reward picked and handed to that function, which builds
the present and saves it like any other. So the plugin keeps no record of its
own and can never give a day twice. The floor tier comes from the elevator
stops the player has open, read with the game's own lookup, and the Express
Pass from the same end time the daily check itself tests.

Which game functions to use, and the idea of hooking the daily check, come
from LetItRevive by Claudia-diva (MIT licence), which hands out the old
calendar's rewards date by date.

Safety rules, all in [dll/lid_loginbonus.c](dll/lid_loginbonus.c):

- It only does anything inside `BrgGame-Steam.exe`. Loaded by any other
  program, it changes nothing.
- The daily check, the reward granter, the elevator-stop lookup, the player
  data and the login count must each be found exactly once by their exact
  bytes, or nothing is changed. After a game update that moves the code, the plugin does nothing
  and says so in its log.
- Changes are made **in memory only**: the first 18 bytes of the daily check.
  The exe on disk is never touched.
- `masters.db` is opened read-only. No network, no registry. The files it
  writes are next to itself: `lid-loginbonus.log`, `lid-loginbonus.seed` (this
  install's random number), and `lid-loginbonus.busy` while a reward is being
  handed over. If the game closes while that file is there, the plugin stays
  off until you delete it, so a crash can never repeat every time the game
  starts.
- Delete `lid-loginbonus.dll` and the game is exactly stock again.

## Why the DLL is built by GitHub

A DLL can do anything, so you should not have to take one on trust. Every
release on this page is built by GitHub Actions from the source in this
repository, on GitHub's own machines, and the tests run before anything is
published. You can check the DLL in two ways:

1. **Signed build record.** Each release DLL has an attestation: a signed
   record, kept by GitHub, of the exact workflow run and commit that built it.
   With the [GitHub CLI](https://cli.github.com/):

   ```
   gh attestation verify lid-loginbonus.dll --repo NicoMullin/LiD-Login-Bonus
   ```

2. **Rebuild it yourself.** The build is reproducible: the same source and the
   same compiler give a byte-identical file. See
   [Building it yourself](#building-it-yourself) and compare the SHA-256 with
   the one on the release page.

## Installing

**The easy way.** In LiD DB Editor (the LET IT DIE DB Mod Manager) 0.12.6 or
newer, close the game, click **Get mods...** and then **Get** next to **Login
Bonus**. It downloads this repository's newest release, checks it against the
SHA-256 GitHub lists for it, and once you say yes puts the Login Bonus mod in
the manager and `lid-loginbonus.dll` in the game's `Binaries\Win64\lid-plugins`
- with the [LiD Plugin Loader](https://github.com/NicoMullin/LiD-Plugin-Loader)
too, if you do not have it yet. Then tick **Login Bonus** and click **Save Mod
List**. The mod is optional: without it the plugin uses the online game's
lists.

**By hand.**

1. Install the [LiD Plugin Loader](https://github.com/NicoMullin/LiD-Plugin-Loader)
   once, if you have not already. It puts a `dinput8.dll` and a `lid-plugins`
   folder in the game's `Binaries\Win64` folder.
2. Download `LiD-Login-Bonus.zip` from the
   [latest release](https://github.com/NicoMullin/LiD-Login-Bonus/releases/latest).
   Put `lid-plugins\lid-loginbonus.dll` from it in the game's `Binaries\Win64\lid-plugins`.
3. In the manager, drop the `mods\Login Bonus` folder from the zip onto it,
   tick it and click **Save Mod List**. (Optional - see above.)

The release also has `lid-loginbonus.dll` on its own, for anyone who only wants the plugin.

It works alongside other plugins, such as LiD Death Metals, LiD Timers and
Fighter Class Passives: they change different parts of the game.

## Building it yourself

You need Windows and Python 3. The compiler is Zig, installed with pip; no
Visual Studio needed.

```
py -3 -m pip install ziglang==0.16.0
py -3 build.py --test
```

The DLL lands in `out\lid-loginbonus.dll` and the SHA-256 is printed at the
end. Use Zig 0.16.0 exactly: another version builds a working DLL, but not a
byte-identical one.

The plugin reads `masters.db` with [SQLite](https://www.sqlite.org/) (public
domain). Its source is not kept here: the first build downloads the official
amalgamation (3.53.4) from sqlite.org into `third_party\` and refuses it unless
its SHA3-256 matches the one sqlite.org publishes.

`--test` builds stand-ins for the game exe, with the daily check, the reward
granter and the elevator-stop function in the game's byte shapes and a small
`masters.db`, and checks that:

- on a day already counted, nothing is handed out;
- on a new day, the game still records the day and one reward from the 1-20F
  list is handed out, from `LOGIN`; the same day again without saving gives
  the same one, and new days started early on the same date give new ones;
- with the Express Pass, two rewards; with the 21F/31F stop open, the 21-40F
  list; with the 41F/51F stop, the 41F+ list;
- over 400 days every reward comes up about as often, and a reward missing
  from `master_reward`, or one that would arrive as several presents, never
  does;
- a mod's own `LID_RANDOM_O41` list replaces only that tier's list;
- with `lid-loginbonus.busy` left over, or no `masters.db`, nothing is handed
  out; with none of the game code, nothing is changed and the log says so.

`--exe PATH\BrgGame-Steam.exe` also runs `tests/realexe.c`: the plugin's own
search on the real game file (mapped, not run), which must find every piece.
The game is not in this repository, so GitHub does not run it.

## Making a release

1. Set the new version in [dll/version.h](dll/version.h).
2. Push a tag with the same version, for example `v0.1.0`.

GitHub builds, tests and publishes the release. The build stops if the tag and
`version.h` don't match.

## License

MIT - see [LICENSE](LICENSE). Made by KSFA.
