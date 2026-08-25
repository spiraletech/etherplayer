# ETHERPLAYER

**EtherPlayer v0.1** is the focused listening interface inside the **EtherPlay** platform.

EtherPlay manages the collection. EtherPlayer experiences the collection.

## v0.1 product modes

- **Hero** — approved Big Screen Now Playing composition: centered art, horizontal analyzer, hardware-style controls.
- **Browse** — Zune-inspired typography navigation for music, playlists, artists, albums, songs, and queue.
- **Queue** — Up Next, Play Next, reorder-ready queue model.
- **PIP** — compact always-on-top presentation of the same player state.
- **Remote** — couch/bed control-deck presentation. v0.1 is local UI; networking is intentionally deferred.

## Architecture

This repository is intentionally separate from the current EtherPlay Windows application so the player interaction model can evolve without destabilizing EtherPlay v1 // POLISHED.

The prototype still treats EtherPlay as the platform mothership:

- Reads the current Windows user's EtherPlay library from `%LOCALAPPDATA%\EtherPlay\profiles\<user>\library.txt` when available.
- Uses one authoritative EtherPlayer `PlayerState` for track, queue, mode, presentation size and selection.
- Does **not** create a second competing music library database.
- The standalone `ETHERPLAYER.exe` is an engineering harness. The same module is intended to embed into `ETHERPLAY.exe` later.

## Controls

### Now Playing

| Control | Action |
| --- | --- |
| Center | Play / Pause |
| Left tap | Restart track; double tap = previous |
| Left hold | Rewind |
| Right tap | Next |
| Right hold | Fast-forward |
| Up | Home |
| Down | Queue |
| Ring / wheel | Volume |

### Browse

| Control | Action |
| --- | --- |
| Up / Down or ring | Move selection |
| Center | Select |
| Left | Back |
| Right | Quick Action: Play Next / Add to Queue / Playlist / Song Info |

### Queue / Seek

Down opens Queue. Holding Down is reserved for precision Seek mode. The same ring/wheel becomes the scrub control.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release --target EtherPlayer
```

Windows CI publishes an `ETHERPLAYER.exe` artifact on pull requests and manual runs.

## Design rule

**Now Playing controls music. Browse controls hierarchy. Queue controls sequence.**

Black glass, warm amber/gold analyzer, centered art, restrained typography. No expensive particle renderer in v0.1.
