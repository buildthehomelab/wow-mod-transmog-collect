# Transmog Collect

> **Archived.** Superseded by [wow-mod-transmog-plus](https://github.com/buildthehomelab/wow-mod-transmog-plus), which replaces mod-transmog and collects appearances on pickup, disenchant and login itself.

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) add-on for
[mod-transmog](https://github.com/azerothcore/mod-transmog). With mod-transmog's collection system,
an appearance unlocks when you equip the item, loot a soulbound one, or finish a quest that
rewards it. This module adds more ways to unlock one:

- **Picking it up.** Any weapon or armor piece that lands in your bags unlocks its appearance,
  bound or not. That covers loot, vendors, quest rewards, crafting, trades, mail (so the auction
  house too) and the guild bank. Loot a green BoE, vendor it, and you keep the look.
- **Disenchanting it.** Disenchanting an item unlocks its appearance. With pick-up unlocking on,
  this only matters for items you had before the module was installed. Turn pick-up off and
  disenchanting becomes the way to collect.
- **Having it on login.** Everything in your bags and bank is collected when you log in, with one
  chat line saying how many appearances were new.

Each new appearance gets the usual chat line, for example
`[Tough Scorpid Breastplate] has been added to your appearance collection.`

Appearances go into mod-transmog's own collection, account-wide, and show up at the
transmogrifier immediately. mod-transmog's `Transmogrification.TrackUnusableItems` still decides
whether appearances your character can't wear are collected.

## Playerbots

Random bots never collect. They have accounts of their own, and mod-transmog loads the whole
collection table into memory at startup, so thousands of looting bots would bloat it. Your own
characters played as bots (alt bots) share your account, and they collect for it while you're
grouped with them. `TransmogCollect.AltBots = 0` turns that off.

On stock AzerothCore without playerbots this section doesn't apply.

## Requirements

- mod-transmog, installed in the same `modules` folder, with
  `Transmogrification.UseCollectionSystem = 1` (its default). The module calls into mod-transmog
  directly, so the worldserver must build modules statically (AzerothCore's default). It only uses
  mod-transmog functions that date from 2022, so older mod-transmog checkouts work too.
- No SQL. The module writes to mod-transmog's `custom_unlocked_appearances` table.

## Installation

Clone it into your AzerothCore `modules` folder, **as `mod-transmog-collect`**. The folder name
matters, because AzerothCore derives the module's loader name from it:

```bash
cd modules
git clone https://github.com/buildthehomelab/wow-mod-transmog-collect.git mod-transmog-collect
```

Re-run CMake and rebuild the worldserver, then copy `conf/mod_transmog_collect.conf.dist` to your
config folder as `mod_transmog_collect.conf` if you want to change anything.

## Configuration

| Option | Default | What it does |
| --- | --- | --- |
| `TransmogCollect.Enable` | 1 | Master switch. |
| `TransmogCollect.OnPickup` | 1 | Unlock appearances when items reach your bags. |
| `TransmogCollect.OnDisenchant` | 1 | Unlock appearances when you disenchant. |
| `TransmogCollect.ScanOnLogin` | 1 | Collect your bags and bank on every login. |
| `TransmogCollect.AltBots` | 1 | Let your alt bots collect for your account while grouped with you. |
| `TransmogCollect.Announce` | 1 | Chat line per new appearance. Skipped for players with `.transmog off`, as in mod-transmog. |

## License

MIT
