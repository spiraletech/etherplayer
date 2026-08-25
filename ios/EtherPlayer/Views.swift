import SwiftUI
import UniformTypeIdentifiers

struct RootView: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var store: LibraryStore
    @EnvironmentObject var audio: AudioEngine

    var body: some View {
        ZStack {
            EPTheme.black.ignoresSafeArea()
            VStack(spacing: 0) {
                header
                Group {
                    switch model.screen {
                    case .hero: HeroView()
                    case .browse: BrowseView()
                    case .queue: QueueView()
                    case .remote: RemoteView()
                    case .settings: SettingsView()
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)

                if model.screen != .hero, model.currentTrack != nil {
                    MiniPlayerView()
                        .padding(.horizontal, 14)
                        .padding(.bottom, 8)
                }
                nav
            }
        }
        .tint(EPTheme.gold)
    }

    private var header: some View {
        HStack(alignment: .firstTextBaseline) {
            VStack(alignment: .leading, spacing: 0) {
                Text("ETHERPLAYER")
                    .font(.system(size: 20, weight: .black, design: .rounded))
                Text("v0.1 // iOS")
                    .font(.system(size: 9, weight: .bold, design: .monospaced))
                    .foregroundStyle(EPTheme.muted)
            }
            Spacer()
            Text("E")
                .font(.system(size: 17, weight: .black, design: .rounded))
                .foregroundStyle(EPTheme.gold)
        }
        .padding(.horizontal, 20)
        .padding(.top, 12)
        .padding(.bottom, 8)
    }

    private var nav: some View {
        HStack(spacing: 5) {
            navButton(.hero, icon: "play.circle", title: "hero")
            navButton(.browse, icon: "music.note.list", title: "music")
            navButton(.queue, icon: "text.line.first.and.arrowtriangle.forward", title: "queue")
            navButton(.remote, icon: "circle.grid.cross", title: "remote")
            navButton(.settings, icon: "gearshape", title: "settings")
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(Color.black.opacity(0.96))
    }

    private func navButton(_ screen: EPScreen, icon: String, title: String) -> some View {
        Button {
            if screen == .settings { model.openSettings() } else { model.screen = screen }
        } label: {
            VStack(spacing: 3) {
                Image(systemName: icon).font(.system(size: 15, weight: .semibold))
                Text(title).font(.system(size: 8, weight: .bold, design: .rounded))
            }
            .foregroundStyle(model.screen == screen ? EPTheme.gold : EPTheme.muted)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 7)
            .background(model.screen == screen ? EPTheme.gold.opacity(0.08) : Color.clear)
            .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
        }
    }
}

struct HeroView: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var store: LibraryStore
    @EnvironmentObject var audio: AudioEngine
    @State private var importingMusic = false

    var body: some View {
        ScrollView(showsIndicators: false) {
            VStack(spacing: 16) {
                device
                Button("ADD MUSIC") { importingMusic = true }
                    .buttonStyle(GoldCapsuleButtonStyle())
                    .padding(.bottom, 18)
            }
            .padding(.horizontal, 18)
            .padding(.top, 4)
        }
        .fileImporter(isPresented: $importingMusic, allowedContentTypes: [.audio], allowsMultipleSelection: true) { result in
            if case .success(let urls) = result { store.importAudio(urls: urls) }
        }
    }

    private var device: some View {
        VStack(spacing: 12) {
            HStack {
                Spacer()
                Button { model.openSettings() } label: {
                    Image(systemName: "gearshape.fill")
                        .font(.system(size: 13, weight: .bold))
                        .foregroundStyle(EPTheme.muted)
                }
                Text("E")
                    .font(.system(size: 13, weight: .black, design: .rounded))
                    .foregroundStyle(EPTheme.gold)
                    .padding(.leading, 22)
                Spacer()
            }
            .frame(height: 26)

            if let track = model.currentTrack {
                TrackArtwork(url: store.artworkURL(for: track), size: 220)
                Text("NOW PLAYING")
                    .font(.system(size: 9, weight: .black, design: .rounded))
                    .foregroundStyle(EPTheme.gold)
                Text(track.displayTitle)
                    .font(.system(size: 24, weight: .black, design: .rounded))
                    .lineLimit(2)
                    .multilineTextAlignment(.center)
                Text(track.displayArtist)
                    .font(.system(size: 12, weight: .medium, design: .rounded))
                    .foregroundStyle(EPTheme.muted)

                FrequencyBars(bands: audio.bands)
                    .frame(height: 82)
                    .padding(.horizontal, 10)

                VStack(spacing: 3) {
                    Slider(value: Binding(get: { audio.position }, set: { model.seek(to: $0) }), in: 0...max(1, audio.duration))
                    HStack {
                        Text(epTime(audio.position))
                        Spacer()
                        Text(epTime(audio.duration))
                    }
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .foregroundStyle(EPTheme.muted)
                }
                .padding(.horizontal, 10)

                TransportBay()
            } else {
                TrackArtwork(url: nil, size: 220)
                Text("NO TRACK LOADED")
                    .font(.system(size: 18, weight: .black, design: .rounded))
                Text("Import MP3 or WAV from Files")
                    .font(.system(size: 12, design: .rounded))
                    .foregroundStyle(EPTheme.muted)
                FrequencyBars(bands: audio.bands)
                    .frame(height: 72)
                    .padding(.horizontal, 10)
            }
        }
        .padding(18)
        .epPanel(radius: 32)
    }
}

struct TransportBay: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var audio: AudioEngine

    var body: some View {
        HStack(spacing: 22) {
            Button { model.previous() } label: {
                Image(systemName: "backward.fill").frame(width: 46, height: 46)
            }
            Button { model.togglePlayback() } label: {
                Image(systemName: audio.isPlaying ? "pause.fill" : "play.fill")
                    .font(.system(size: 25, weight: .black))
                    .frame(width: 66, height: 66)
                    .background(Color.black)
                    .clipShape(Circle())
                    .overlay(Circle().stroke(EPTheme.gold, lineWidth: 2))
            }
            Button { model.next() } label: {
                Image(systemName: "forward.fill").frame(width: 46, height: 46)
            }
        }
        .buttonStyle(.plain)
        .foregroundStyle(.white)
        .frame(maxWidth: .infinity)
        .padding(.vertical, 10)
        .epPanel(radius: 24)
    }
}

struct BrowseView: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var store: LibraryStore

    var body: some View {
        VStack(spacing: 10) {
            HStack(spacing: 8) {
                ForEach(EPBrowseMode.allCases) { mode in
                    Button(mode.rawValue) { model.browseMode = mode }
                        .font(.system(size: 13, weight: .bold, design: .rounded))
                        .foregroundStyle(model.browseMode == mode ? .black : .white)
                        .padding(.horizontal, 16).padding(.vertical, 9)
                        .background(model.browseMode == mode ? EPTheme.gold : EPTheme.panel)
                        .clipShape(Capsule())
                }
                Spacer()
            }
            .padding(.horizontal, 18)

            ScrollView {
                LazyVStack(spacing: 8) {
                    if store.tracks.isEmpty {
                        ContentUnavailableView("No Music Yet", systemImage: "music.note", description: Text("Add music from Hero to build your local EtherPlayer library."))
                            .foregroundStyle(.white)
                            .padding(.top, 70)
                    } else {
                        switch model.browseMode {
                        case .music:
                            ForEach(store.tracks) { track in TrackRow(track: track) }
                        case .artists:
                            ForEach(artistGroups, id: \.0) { artist, tracks in
                                GroupHeader(title: artist)
                                ForEach(tracks) { track in TrackRow(track: track) }
                            }
                        case .albums:
                            ForEach(albumGroups, id: \.0) { album, tracks in
                                GroupHeader(title: album)
                                ForEach(tracks) { track in TrackRow(track: track) }
                            }
                        }
                    }
                }
                .padding(.horizontal, 14)
                .padding(.bottom, 24)
            }
        }
    }

    private var artistGroups: [(String, [EPTrack])] {
        Dictionary(grouping: store.tracks, by: { $0.displayArtist })
            .map { ($0.key, $0.value) }.sorted { $0.0.localizedCaseInsensitiveCompare($1.0) == .orderedAscending }
    }

    private var albumGroups: [(String, [EPTrack])] {
        Dictionary(grouping: store.tracks, by: { $0.album.isEmpty ? "Unknown Album" : $0.album })
            .map { ($0.key, $0.value) }.sorted { $0.0.localizedCaseInsensitiveCompare($1.0) == .orderedAscending }
    }
}

struct GroupHeader: View {
    let title: String
    var body: some View {
        HStack {
            Text(title).font(.system(size: 18, weight: .black, design: .rounded)).foregroundStyle(EPTheme.gold)
            Spacer()
        }
        .padding(.top, 10).padding(.horizontal, 4)
    }
}

struct TrackRow: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var store: LibraryStore
    let track: EPTrack

    var body: some View {
        Button { model.play(track) } label: {
            HStack(spacing: 12) {
                TrackArtwork(url: store.artworkURL(for: track), size: 52)
                VStack(alignment: .leading, spacing: 4) {
                    Text(track.displayTitle).font(.system(size: 15, weight: .bold, design: .rounded)).lineLimit(1)
                    Text(track.displayArtist).font(.system(size: 11, design: .rounded)).foregroundStyle(EPTheme.muted).lineLimit(1)
                }
                Spacer()
                Image(systemName: "chevron.right").foregroundStyle(EPTheme.gold)
            }
            .padding(9)
            .epPanel(radius: 16)
        }
        .buttonStyle(.plain)
        .contextMenu {
            Button("Play Next", systemImage: "text.insert") { model.playNext(track) }
            Button("Edit Metadata", systemImage: "gearshape") { model.openSettings(for: track) }
            Button("Remove from Library", systemImage: "trash", role: .destructive) { store.removeFromLibrary(track.id) }
        }
    }
}

struct QueueView: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var store: LibraryStore

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("UP NEXT").font(.system(size: 30, weight: .black, design: .rounded))
                    Text("drag to reorder • swipe to remove").font(.system(size: 10, weight: .bold, design: .rounded)).foregroundStyle(EPTheme.muted)
                }
                Spacer()
                EditButton().foregroundStyle(EPTheme.gold)
            }
            .padding(.horizontal, 18)

            List {
                ForEach(Array(store.queueTracks().enumerated()), id: \.element.id) { index, track in
                    HStack {
                        Text("\(index + 1)").foregroundStyle(EPTheme.gold).font(.system(size: 12, weight: .bold, design: .monospaced)).frame(width: 24)
                        VStack(alignment: .leading) {
                            Text(track.displayTitle).font(.system(size: 15, weight: .bold, design: .rounded))
                            Text(track.displayArtist).font(.system(size: 10, design: .rounded)).foregroundStyle(EPTheme.muted)
                        }
                        Spacer()
                        if store.selectedTrackID == track.id { Image(systemName: "waveform").foregroundStyle(EPTheme.gold) }
                    }
                    .contentShape(Rectangle())
                    .onTapGesture { model.playID(track.id) }
                    .listRowBackground(EPTheme.panel)
                }
                .onMove(perform: store.moveQueue)
                .onDelete(perform: store.removeQueue)
            }
            .scrollContentBackground(.hidden)
            .background(EPTheme.black)
            .environment(\.editMode, .constant(.active))
        }
    }
}

struct SettingsView: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var store: LibraryStore
    @State private var draft: EPTrack?
    @State private var importingArtwork = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text("settings")
                    .font(.system(size: 38, weight: .black, design: .rounded))
                Text("SONG IDENTITY // ETHERPLAYER METADATA")
                    .font(.system(size: 10, weight: .black, design: .rounded))
                    .foregroundStyle(EPTheme.gold)

                if var active = draft {
                    VStack(spacing: 12) {
                        TrackArtwork(url: store.artworkURL(for: active), size: 170)
                        Button("CHOOSE ARTWORK") { importingArtwork = true }
                            .buttonStyle(GoldCapsuleButtonStyle())
                    }
                    .frame(maxWidth: .infinity)

                    metadataField("TITLE", text: binding(&active.title))
                    metadataField("ARTIST", text: binding(&active.artist))
                    metadataField("ALBUM", text: binding(&active.album))
                    metadataField("GENRE", text: binding(&active.genre))
                    HStack {
                        metadataField("YEAR", text: binding(&active.year))
                        metadataField("TRACK", text: binding(&active.trackNumber))
                        metadataField("BPM", text: binding(&active.bpm))
                    }
                    metadataField("COMMENT", text: binding(&active.comment), axis: .vertical)

                    Button("SAVE SONG INFO") {
                        draft = active
                        model.saveMetadata(active)
                    }
                    .buttonStyle(GoldCapsuleButtonStyle())
                    .frame(maxWidth: .infinity)
                    .padding(.top, 4)
                    .onChange(of: active) { _, newValue in draft = newValue }
                } else {
                    ContentUnavailableView("No Track Selected", systemImage: "gearshape", description: Text("Play or select a song, then return to Settings."))
                        .foregroundStyle(.white)
                        .padding(.top, 70)
                }
            }
            .padding(.horizontal, 18)
            .padding(.bottom, 30)
        }
        .onAppear { draft = model.editingTrack }
        .onChange(of: model.editingTrackID) { _, _ in draft = model.editingTrack }
        .fileImporter(isPresented: $importingArtwork, allowedContentTypes: [.image], allowsMultipleSelection: false) { result in
            guard case .success(let urls) = result, let url = urls.first, let id = draft?.id else { return }
            model.importArtwork(url, for: id)
        }
    }

    private func binding(_ value: inout String) -> Binding<String> {
        let original = value
        return Binding(
            get: { draftValue(for: original) },
            set: { newValue in
                guard var copy = draft else { return }
                if original == copy.title { copy.title = newValue }
                else if original == copy.artist { copy.artist = newValue }
                else if original == copy.album { copy.album = newValue }
                else if original == copy.genre { copy.genre = newValue }
                else if original == copy.year { copy.year = newValue }
                else if original == copy.trackNumber { copy.trackNumber = newValue }
                else if original == copy.bpm { copy.bpm = newValue }
                else { copy.comment = newValue }
                draft = copy
            }
        )
    }

    private func draftValue(for original: String) -> String { original }

    private func metadataField(_ label: String, text: Binding<String>, axis: Axis = .horizontal) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(label).font(.system(size: 9, weight: .black, design: .rounded)).foregroundStyle(EPTheme.muted)
            TextField(label, text: text, axis: axis)
                .font(.system(size: 14, weight: .semibold, design: .rounded))
                .padding(12)
                .background(Color.white.opacity(0.055))
                .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
        }
        .frame(maxWidth: .infinity)
    }
}

struct RemoteView: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var audio: AudioEngine

    var body: some View {
        VStack(spacing: 18) {
            VStack(spacing: 3) {
                Text("REMOTE").font(.system(size: 34, weight: .black, design: .rounded))
                Text("ETHERPLAYER CONTROL DECK").font(.system(size: 10, weight: .black, design: .rounded)).foregroundStyle(EPTheme.gold)
                Text("LOCAL PLAYER STATE // PC LAN BRIDGE NEXT").font(.system(size: 8, weight: .bold, design: .monospaced)).foregroundStyle(EPTheme.muted)
            }

            VStack(spacing: 10) {
                Button { model.screen = .hero } label: { Label("HOME / UP", systemImage: "house") }
                    .foregroundStyle(EPTheme.gold)
                    .font(.system(size: 14, weight: .black, design: .rounded))

                HStack(spacing: 20) {
                    Button { model.previous() } label: { Image(systemName: "backward.fill") }
                    Button { model.togglePlayback() } label: {
                        Image(systemName: audio.isPlaying ? "pause.fill" : "play.fill")
                            .font(.system(size: 27, weight: .black))
                            .frame(width: 92, height: 92)
                            .overlay(Circle().stroke(EPTheme.gold, lineWidth: 2))
                    }
                    Button { model.next() } label: { Image(systemName: "forward.fill") }
                }
                .font(.system(size: 20, weight: .bold))
                .foregroundStyle(.white)

                Button { model.screen = .queue } label: { Label("QUEUE / SEEK DOWN", systemImage: "text.line.first.and.arrowtriangle.forward") }
                    .foregroundStyle(EPTheme.gold)
                    .font(.system(size: 14, weight: .black, design: .rounded))
            }
            .frame(maxWidth: .infinity)
            .padding(22)
            .epPanel(radius: 120)
            .padding(.horizontal, 34)

            FrequencyBars(bands: audio.bands, compact: true)
                .frame(height: 54)
                .padding(12)
                .epPanel(radius: 16)
                .padding(.horizontal, 24)

            Text("LIVE 72-BAND // ETHERPLAYER AUDIO BUS")
                .font(.system(size: 7, weight: .bold, design: .monospaced))
                .foregroundStyle(EPTheme.muted)
            Spacer(minLength: 0)
        }
        .padding(.top, 12)
    }
}

struct MiniPlayerView: View {
    @EnvironmentObject var model: PlayerModel
    @EnvironmentObject var store: LibraryStore
    @EnvironmentObject var audio: AudioEngine

    var body: some View {
        if let track = model.currentTrack {
            HStack(spacing: 10) {
                TrackArtwork(url: store.artworkURL(for: track), size: 42)
                VStack(alignment: .leading, spacing: 2) {
                    Text(track.displayTitle).font(.system(size: 12, weight: .bold, design: .rounded)).lineLimit(1)
                    FrequencyBars(bands: audio.bands, compact: true).frame(height: 18)
                }
                Spacer()
                Button { model.previous() } label: { Image(systemName: "backward.fill") }
                Button { model.togglePlayback() } label: { Image(systemName: audio.isPlaying ? "pause.fill" : "play.fill") }
                Button { model.next() } label: { Image(systemName: "forward.fill") }
            }
            .foregroundStyle(.white)
            .padding(8)
            .epPanel(radius: 16)
            .onTapGesture { model.screen = .hero }
        }
    }
}
