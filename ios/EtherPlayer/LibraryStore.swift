import Foundation

@MainActor
final class LibraryStore: ObservableObject {
    @Published private(set) var tracks: [EPTrack] = []
    @Published private(set) var queue: [UUID] = []
    @Published var selectedTrackID: UUID?

    private struct Snapshot: Codable {
        var tracks: [EPTrack]
        var queue: [UUID]
        var selectedTrackID: UUID?
    }

    private let fileManager = FileManager.default

    init() {
        try? fileManager.createDirectory(at: mediaDirectory, withIntermediateDirectories: true)
        try? fileManager.createDirectory(at: artworkDirectory, withIntermediateDirectories: true)
        load()
    }

    var documentsDirectory: URL {
        fileManager.urls(for: .documentDirectory, in: .userDomainMask).first!
    }

    var mediaDirectory: URL { documentsDirectory.appendingPathComponent("EtherPlayerMedia", isDirectory: true) }
    var artworkDirectory: URL { documentsDirectory.appendingPathComponent("EtherPlayerArtwork", isDirectory: true) }
    private var snapshotURL: URL { documentsDirectory.appendingPathComponent("etherplayer-library.json") }

    func mediaURL(for track: EPTrack) -> URL {
        mediaDirectory.appendingPathComponent(track.localFilename)
    }

    func artworkURL(for track: EPTrack) -> URL? {
        guard let name = track.artworkFilename else { return nil }
        return artworkDirectory.appendingPathComponent(name)
    }

    func track(id: UUID?) -> EPTrack? {
        guard let id else { return nil }
        return tracks.first(where: { $0.id == id })
    }

    func index(of id: UUID) -> Int? { tracks.firstIndex(where: { $0.id == id }) }

    func queueTracks() -> [EPTrack] {
        queue.compactMap { id in tracks.first(where: { $0.id == id }) }
    }

    func importAudio(urls: [URL]) {
        for source in urls {
            let scoped = source.startAccessingSecurityScopedResource()
            defer { if scoped { source.stopAccessingSecurityScopedResource() } }
            do {
                let safeName = uniqueFilename(for: source.lastPathComponent, in: mediaDirectory)
                let destination = mediaDirectory.appendingPathComponent(safeName)
                try fileManager.copyItem(at: source, to: destination)
                let title = source.deletingPathExtension().lastPathComponent
                let item = EPTrack(localFilename: safeName, title: title)
                tracks.append(item)
                if queue.isEmpty { queue.append(item.id) }
                selectedTrackID = item.id
            } catch {
                print("EtherPlayer import failed: \(error)")
            }
        }
        save()
    }

    func importArtwork(url: URL, for trackID: UUID) {
        guard let idx = index(of: trackID) else { return }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            let ext = url.pathExtension.isEmpty ? "png" : url.pathExtension
            let name = "\(trackID.uuidString).\(ext)"
            let destination = artworkDirectory.appendingPathComponent(name)
            try? fileManager.removeItem(at: destination)
            try fileManager.copyItem(at: url, to: destination)
            tracks[idx].artworkFilename = name
            save()
        } catch {
            print("EtherPlayer artwork import failed: \(error)")
        }
    }

    func playTrack(_ id: UUID, enqueueRest: Bool = true) {
        selectedTrackID = id
        if enqueueRest, let idx = index(of: id) {
            queue = Array(tracks[idx...].map(\.id))
        } else if !queue.contains(id) {
            queue.append(id)
        }
        save()
    }

    func playNext(_ id: UUID) {
        if queue.isEmpty {
            queue = [id]
        } else if let current = selectedTrackID, let currentQueueIndex = queue.firstIndex(of: current) {
            queue.removeAll(where: { $0 == id })
            queue.insert(id, at: min(currentQueueIndex + 1, queue.count))
        } else {
            queue.insert(id, at: 0)
        }
        save()
    }

    func nextID() -> UUID? {
        guard !queue.isEmpty else { return nil }
        guard let current = selectedTrackID, let idx = queue.firstIndex(of: current) else { return queue.first }
        return queue[(idx + 1) % queue.count]
    }

    func previousID() -> UUID? {
        guard !queue.isEmpty else { return nil }
        guard let current = selectedTrackID, let idx = queue.firstIndex(of: current) else { return queue.first }
        return queue[(idx - 1 + queue.count) % queue.count]
    }

    func moveQueue(from source: IndexSet, to destination: Int) {
        let indexes = source.sorted()
        let moving = indexes.map { queue[$0] }
        for index in indexes.reversed() { queue.remove(at: index) }
        let removedBefore = indexes.filter { $0 < destination }.count
        let insertion = max(0, min(queue.count, destination - removedBefore))
        queue.insert(contentsOf: moving, at: insertion)
        save()
    }

    func removeQueue(at offsets: IndexSet) {
        let removedIDs = offsets.compactMap { queue.indices.contains($0) ? queue[$0] : nil }
        for index in offsets.sorted(by: >) where queue.indices.contains(index) { queue.remove(at: index) }
        if let selected = selectedTrackID, removedIDs.contains(selected), !queue.contains(selected) {
            selectedTrackID = queue.first
        }
        save()
    }

    func updateMetadata(_ updated: EPTrack) {
        guard let idx = index(of: updated.id) else { return }
        tracks[idx] = updated
        save()
    }

    func removeFromLibrary(_ id: UUID) {
        guard let idx = index(of: id) else { return }
        let doomed = tracks[idx]
        try? fileManager.removeItem(at: mediaURL(for: doomed))
        if let art = artworkURL(for: doomed) { try? fileManager.removeItem(at: art) }
        tracks.remove(at: idx)
        queue.removeAll(where: { $0 == id })
        if selectedTrackID == id { selectedTrackID = queue.first ?? tracks.first?.id }
        save()
    }

    func save() {
        let snapshot = Snapshot(tracks: tracks, queue: queue, selectedTrackID: selectedTrackID)
        do {
            let data = try JSONEncoder().encode(snapshot)
            try data.write(to: snapshotURL, options: .atomic)
        } catch {
            print("EtherPlayer library save failed: \(error)")
        }
    }

    private func load() {
        guard let data = try? Data(contentsOf: snapshotURL),
              let snapshot = try? JSONDecoder().decode(Snapshot.self, from: data) else { return }
        tracks = snapshot.tracks.filter { fileManager.fileExists(atPath: mediaURL(for: $0).path) }
        let valid = Set(tracks.map(\.id))
        queue = snapshot.queue.filter { valid.contains($0) }
        selectedTrackID = snapshot.selectedTrackID.flatMap { valid.contains($0) ? $0 : nil } ?? queue.first ?? tracks.first?.id
    }

    private func uniqueFilename(for proposed: String, in directory: URL) -> String {
        let source = URL(fileURLWithPath: proposed)
        let base = source.deletingPathExtension().lastPathComponent
        let ext = source.pathExtension
        var candidate = proposed
        var serial = 2
        while fileManager.fileExists(atPath: directory.appendingPathComponent(candidate).path) {
            candidate = ext.isEmpty ? "\(base) \(serial)" : "\(base) \(serial).\(ext)"
            serial += 1
        }
        return candidate
    }
}
