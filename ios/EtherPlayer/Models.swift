import Foundation

enum EPScreen: String, CaseIterable, Identifiable {
    case hero, browse, queue, remote, settings
    var id: String { rawValue }
}

enum EPBrowseMode: String, CaseIterable, Identifiable {
    case music, artists, albums
    var id: String { rawValue }
}

struct EPTrack: Codable, Identifiable, Hashable {
    var id: UUID = UUID()
    var localFilename: String
    var title: String
    var artist: String = "unknown artist"
    var album: String = ""
    var genre: String = ""
    var year: String = ""
    var trackNumber: String = ""
    var bpm: String = ""
    var comment: String = ""
    var artworkFilename: String? = nil

    var displayTitle: String {
        let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? URL(fileURLWithPath: localFilename).deletingPathExtension().lastPathComponent : trimmed
    }

    var displayArtist: String {
        let trimmed = artist.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "unknown artist" : trimmed
    }
}
