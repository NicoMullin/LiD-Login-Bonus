# Login Bonus

Chooses what the daily login bonus draws from. The bonus itself is handed out
by **lid-loginbonus.dll**, a plugin for the LiD Plugin Loader: without it, this
mod changes nothing you can see. Get it from
[LiD Login Bonus](https://github.com/NicoMullin/LiD-Login-Bonus).

Each new game day, the first time you reach the hub, the plugin picks a random
reward from the list for your floor tier and puts it in your Rewards Box. With
the Express Pass active, you get a second one from the same list.

## Settings

| Setting                     | What it does                                                                                                                                                                                      |
|-----------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Bonus lists: As online      | The plugin's own lists: every reward the online game's login calendar gave at your floor tier that arrives as one present, each as likely as the others. The same as the plugin without this mod. |
| Bonus lists: Your own lists | Only the rewards on your three lists below.                                                                                                                                                       |
| Your own lists: 1-20F       | Used until the 21F elevator stop is open.                                                                                                                                                         |
| Your own lists: 21-40F      | Used once the 21F or 31F elevator stop is open.                                                                                                                                                   |
| Your own lists: 41F and up  | Used once the 41F or 51F elevator stop is open.                                                                                                                                                   |

Each list starts as the online list for that tier (65, 64 and 62 rewards).
**Edit the list...** opens a window with every reward in your `masters.db`:
Kill Coins, SPLithium, Death Metals, Express Passes, items, mushrooms, decals,
Lost Bags, beasts, weapons and more, each described by what it gives.

- **Weight** is how often a reward comes up compared with the others: weight 3
  comes up three times as often as weight 1. **Chance** shows what that works
  out to.
- **Presents** is how many presents the reward puts in the Rewards Box. The
  game makes one present per item, so "D.O.D. ARMS Purple Metal x4" is 4
  presents. The online lists leave those out; on your own lists they are up to
  you.

## What it changes

`master_login_bonus` only: in "Your own lists" it adds rows with `cond_type`
`LID_RANDOM`, `LID_RANDOM_O21` and `LID_RANDOM_O41` (the reward, and its weight
in `cond_days`), which the plugin reads instead of the online lists. The game
itself reads none of that table offline, so nothing else changes. Switching
the mod off removes them.

Needs LET IT DIE DB Mod Manager 0.12.5 or newer.

## Switched with this mod

From LiD DB Editor 0.12.8, saving with this mod ticked switches
`lid-loginbonus.dll` on, and saving with it unticked switches it off: renamed
to `.dll.off`, which the Plugin Loader skips. Nothing is deleted, and ticking
the mod again brings it back. Close the game first. The plugin used to run on
its own too, drawing from the online rewards; now tick this mod to keep it on
(its default, Online rewards, is the same as the plugin alone).
