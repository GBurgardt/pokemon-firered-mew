# FireRed — Mew Beneath the Truck

The old rumor, made playable in Pokémon FireRed. An unofficial fan-made encounter built on [pret/pokefirered](https://github.com/pret/pokefirered).

[Watch the V4 gameplay demo](https://staging.kycode.dev/generated/firered-mew/pmd-v4-20261009/index.html)

## What this adds

- A shaking truck, a mysterious Mew reveal, and Pokémon Mystery Dungeon character animation.
- A dedicated real-time encounter on the S.S. Anne pier: movement, attacks, psychic projectiles and teleporting Mew.
- Native bag consumption, capture odds, party/PC delivery and a persistent one-time capture flag.
- Smaller outlined sprites and recognizable balls thrown by the trainer.

This is a prototype of **one special encounter**, not a conversion of every FireRed battle. The rest of the game keeps its original combat. Full move effects, abilities, status mechanics, party switching and experience rewards are not implemented in this encounter. Back up your save and test with a separate copy.

## Finding Mew

After earning the Soul Badge, speak to the old man in Vermilion City (map position 17,9). He can take you to the truck even after the S.S. Anne has departed. Bring the Rainbow Badge, the Poké Flute and a living Pokémon that knows Strength. Inspect the truck with A from its east/southeast side (58,4). The sailor can take you back.

During the encounter: D-pad moves, A attacks, B throws a ball, Start flees. Balls are selected from your bag in Ultra / Great / Poké order. Mew is level 30. Save normally after capturing it.

## Build

Follow [INSTALL.md](INSTALL.md) to install the upstream toolchain, including agbcc. Then run:

```sh
make -j4 BUILD_DIR=build-release BUILD_NAME=mew
```

The output is `pokemew.gba`. On the tested Apple Silicon setup, GNU CPP was selected with `CPP=/opt/homebrew/bin/cpp-15`.

For an isolated test fixture at the pier (never use with a personal save):

```sh
make -j4 BUILD_DIR=build-lab BUILD_NAME=mew_lab CPP='cpp -DMEW_LAB'
```

The fixture output is `pokemew_lab.gba`. Use the path to GNU CPP if required on your platform. Native scene code lives in `src/mew_encounter.c`; packed graphics are in `src/data/mew_art.h`.

No ROM downloads or personal saves are distributed here. Use your own legally obtained copy. The tested base is English FireRed USA 1.0; other versions are not validated for this feature.

## Credits

- [pret](https://github.com/pret/pokefirered) and its contributors: the FireRed/LeafGreen decompilation and tooling.
- Pokémon Mystery Dungeon character sprites: CHUNSOFT, sourced via [PMD Sprite Repository — Mew](https://github.com/PMDCollab/SpriteCollab/tree/master/sprite/0151) and [Pikachu](https://github.com/PMDCollab/SpriteCollab/tree/master/sprite/0025). Walk/Sleep/Shoot frames adapted to the native GBA scene.
- Reveal effect development assisted by Claude; native encounter implementation and integration by Germán Burgardt with AI-assisted tooling.
- Pokémon and original game assets belong to their respective owners. This project is unofficial and not affiliated with Nintendo, Game Freak or The Pokémon Company.

## Upstream project

The following describes the original unmodified decompilation. Modified builds above do **not** match these hashes.

### Pokémon FireRed and LeafGreen

This is a decompilation of English Pokémon FireRed and LeafGreen.

It builds the following ROM images:

* [**pokefirered.gba**](https://datomatic.no-intro.org/?page=show_record&s=23&n=1616) `sha1: 41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc`
* [**pokeleafgreen.gba**](https://datomatic.no-intro.org/?page=show_record&s=23&n=1617) `sha1: 574fa542ffebb14be69902d1d36f1ec0a4afd71e`
* [**pokefirered_rev1.gba**](https://datomatic.no-intro.org/?page=show_record&s=23&n=1672) `sha1: dd5945db9b930750cb39d00c84da8571feebf417`
* [**pokeleafgreen_rev1.gba**](https://datomatic.no-intro.org/index.php?page=show_record&s=23&n=1668) `sha1: 7862c67bdecbe21d1d69ce082ce34327e1c6ed5e`
* [**pokefirered_switch.gba**](https://datomatic.no-intro.org/index.php?page=show_record&s=23&n=x550) `sha1: baa452d0b24629dd7782cfc07a8984085dde1311`
* [**pokeleafgreen_switch.gba**](https://datomatic.no-intro.org/index.php?page=show_record&s=23&n=x551) `sha1: 62b9fc77549dbc67032eb6cbd0ea6ad3b825690f`

To set up the repository, see [INSTALL.md](INSTALL.md).

For contacts and other pret projects, see [pret.github.io](https://pret.github.io/).
