import SwiftUI

@main
struct EtherPlayerIOSApp: App {
    @StateObject private var model: PlayerModel
    @StateObject private var store: LibraryStore
    @StateObject private var audio: AudioEngine

    init() {
        let created = PlayerModel()
        _model = StateObject(wrappedValue: created)
        _store = StateObject(wrappedValue: created.store)
        _audio = StateObject(wrappedValue: created.audio)
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
                .environmentObject(store)
                .environmentObject(audio)
                .preferredColorScheme(.dark)
        }
    }
}
