# EtherPlayer v0.1 — Product UI Contract

## Relationship to EtherPlay

**EtherPlay v1 // POLISHED** is the Windows platform shell: library, account, Song Lab, analyzer, collection management.

**EtherPlayer v0.1** is the focused player subsystem. It may run:

1. as **Big Screen mode** inside EtherPlay,
2. as **PIP mode** anchored to the EtherPlay process,
3. as a standalone engineering harness during development,
4. eventually as the UI/state layer for physical EtherPlayer hardware.

The standalone harness is not a second consumer platform.

## Approved concept set

### PASS — Hero Big Screen Now Playing

Canonical EtherPlayer identity.

- Centered cover-art object.
- Track title + artist kept secondary to the listening object.
- Horizontal amber frequency analyzer directly associated with the artwork.
- Black-glass control shrine under the analyzer.
- Five-direction hardware language: Home/Up, Back/Previous, Center, Next/Quick Action, Queue/Down.
- Ring/wheel for volume, list movement and scrubbing depending on context.

### REJECTED — Alternate Hero Layout

Do not use the flatter alternate hero from concept #2 as the primary identity.

### PASS — Browse / Menu

- Giant typography navigation.
- Sections: Music, Playlists, Artists, Albums, Songs, Queue.
- Selected category becomes the strongest typographic object.
- Small Now Playing preview remains present where space permits.
- Center selects; Left goes back; Right opens Quick Action.

### PASS — Queue / Playlist

- Up Next list.
- Selected/current row is clearly highlighted.
- Play Next is a first-class action.
- Queue can be reordered later.
- Bottom/Queue hardware action enters this surface from Now Playing.

### PASS — PIP

- Compact always-on-top presentation of the same authoritative player state.
- Tiny cover art, title/artist, progress, mini analyzer, transport.
- Expanding PIP returns to the same EtherPlayer context without reloading state.

### PASS — Remote Mode

Remote Mode is a presentation/control surface, not a second playback engine.

v0.1 scope:
- local control-deck UI,
- play/pause,
- previous/next,
- volume,
- seek,
- queue,
- Play Next,
- playlist actions,
- current song identity.

Future scope:
- LAN/mobile/web companion,
- authenticated control session,
- output-device switching.

## Control contract

### Now Playing

- Center tap — Play/Pause.
- Left tap — Restart current track.
- Left double tap — Previous track.
- Left hold — Rewind.
- Right tap — Next track.
- Right hold — Fast-forward.
- Up tap — Home/Browse root.
- Up hold — future Quickplay / recent / pinned.
- Down tap — Queue.
- Down hold — Seek mode.
- Ring — Volume.

### Browse

- Ring or Up/Down — move selection.
- Center — select/open/play.
- Left — back one hierarchy.
- Right — Quick Action.

Quick Action order:
1. Play Next
2. Add to Queue
3. Add to Playlist
4. Pin / Favorite (future)
5. Song Info

### Queue

- Up/Down/ring — move queue selection.
- Center — play selected queue item.
- Left — back.
- Right — queue-item Quick Action.

### Seek

- Ring — precision scrub.
- Center — commit/return.
- Left/Right — coarse seek optional.

## Visual language

- Matte black / black glass.
- Warm amber/gold as the single primary accent.
- White/off-white typography.
- Minimal glow, subtle reflections, controlled shadows.
- Cover art is an object inside a dark presentation card, never a raw pasted bitmap.
- Horizontal analyzer only for v0.1; no particle/fog/orbit renderer.
- Product should feel like an alien Zune/iPod-class object, not a Windows utility panel.

## Core rule

**Now Playing controls music. Browse controls hierarchy. Queue controls sequence.**
