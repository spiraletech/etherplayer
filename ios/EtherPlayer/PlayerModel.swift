import Foundation
import MediaPlayer
import UIKit

@MainActor
final class PlayerModel: ObservableObject {
    let store = LibraryStore()
    let audio = AudioEngine()

    @Published var screen: EPScreen = .hero
    @Published var browseMode: EPBrowseMode = .music
    @Published var editingTrackID: UUID?

    init() {
        audio.onFinished = { [weak self] in
            Task { @MainActor in self?.next() }
        }
        configureRemoteCommands()
    }

    var currentTrack: EPTrack? { store.track(id: store.selectedTrackID) }
    var editingTrack: EPTrack? { store.track(id: editingTrackID ?? store.selectedTrackID) }

    func play(_ track: EPTrack, enqueueRest: Bool = true) {
        store.playTrack(track.id, enqueueRest: enqueueRest)
        do {
            try audio.load(url: store.mediaURL(for: track), autoplay: true)
            updateNowPlaying()
        } catch {
            print("EtherPlayer playback failed: \(error)")
        }
    }

    func playID(_ id: UUID, enqueueRest: Bool = false) {
        guard let track = store.track(id: id) else { return }
        play(track, enqueueRest: enqueueRest)
    }

    func togglePlayback() {
        audio.toggle()
        updateNowPlaying()
    }

    func next() {
        guard let id = store.nextID() else { return }
        playID(id)
    }

    func previous() {
        if audio.position > 3 {
            audio.seek(to: 0)
            updateNowPlaying()
            return
        }
        guard let id = store.previousID() else { return }
        playID(id)
    }

    func seek(to seconds: Double) {
        audio.seek(to: seconds)
        updateNowPlaying()
    }

    func playNext(_ track: EPTrack) { store.playNext(track.id) }

    func openSettings(for track: EPTrack? = nil) {
        editingTrackID = track?.id ?? store.selectedTrackID
        screen = .settings
    }

    func saveMetadata(_ track: EPTrack) {
        store.updateMetadata(track)
        editingTrackID = track.id
        if store.selectedTrackID == track.id { updateNowPlaying() }
    }

    func importArtwork(_ url: URL, for id: UUID) {
        store.importArtwork(url: url, for: id)
        if store.selectedTrackID == id { updateNowPlaying() }
    }

    func updateNowPlaying() {
        guard let track = currentTrack else {
            MPNowPlayingInfoCenter.default().nowPlayingInfo = nil
            return
        }
        var info: [String: Any] = [
            MPMediaItemPropertyTitle: track.displayTitle,
            MPMediaItemPropertyArtist: track.displayArtist,
            MPMediaItemPropertyAlbumTitle: track.album,
            MPMediaItemPropertyPlaybackDuration: audio.duration,
            MPNowPlayingInfoPropertyElapsedPlaybackTime: audio.position,
            MPNowPlayingInfoPropertyPlaybackRate: audio.isPlaying ? 1.0 : 0.0
        ]
        if let url = store.artworkURL(for: track), let image = UIImage(contentsOfFile: url.path) {
            info[MPMediaItemPropertyArtwork] = MPMediaItemArtwork(boundsSize: image.size) { _ in image }
        }
        MPNowPlayingInfoCenter.default().nowPlayingInfo = info
    }

    private func configureRemoteCommands() {
        let center = MPRemoteCommandCenter.shared()
        center.playCommand.addTarget { [weak self] _ in
            Task { @MainActor in self?.audio.play(); self?.updateNowPlaying() }
            return .success
        }
        center.pauseCommand.addTarget { [weak self] _ in
            Task { @MainActor in self?.audio.pause(); self?.updateNowPlaying() }
            return .success
        }
        center.togglePlayPauseCommand.addTarget { [weak self] _ in
            Task { @MainActor in self?.togglePlayback() }
            return .success
        }
        center.nextTrackCommand.addTarget { [weak self] _ in
            Task { @MainActor in self?.next() }
            return .success
        }
        center.previousTrackCommand.addTarget { [weak self] _ in
            Task { @MainActor in self?.previous() }
            return .success
        }
        center.changePlaybackPositionCommand.addTarget { [weak self] event in
            guard let event = event as? MPChangePlaybackPositionCommandEvent else { return .commandFailed }
            Task { @MainActor in self?.seek(to: event.positionTime) }
            return .success
        }
    }
}
