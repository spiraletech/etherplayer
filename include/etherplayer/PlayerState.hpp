#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace etherplayer {

enum class Screen {
    Hero,
    Browse,
    Queue,
    Remote
};

enum class Presentation {
    BigScreen,
    Pip
};

enum class BrowseSection {
    Music,
    Playlists,
    Artists,
    Albums,
    Songs,
    Queue
};

struct Track {
    std::wstring path;
    std::wstring title;
    std::wstring artist;
};

class PlayerState {
public:
    void loadEtherPlayLibrary();
    void saveEtherPlayLibrary() const;
    std::size_t addTrackPath(const std::wstring& path);
    void setLibrary(std::vector<Track> tracks);

    [[nodiscard]] const std::vector<Track>& library() const noexcept;
    [[nodiscard]] const std::vector<std::size_t>& queue() const noexcept;
    [[nodiscard]] std::size_t queueIndex() const noexcept;
    [[nodiscard]] const Track* currentTrack() const noexcept;

    bool selectLibraryTrack(std::size_t libraryIndex, bool enqueueRest = false);
    bool playQueueIndex(std::size_t queueIndex);
    bool next();
    bool previous();
    void playNext(std::size_t libraryIndex);
    void addToQueue(std::size_t libraryIndex);
    void clearQueue();

    void setScreen(Screen screen) noexcept;
    [[nodiscard]] Screen screen() const noexcept;

    void setPresentation(Presentation presentation) noexcept;
    [[nodiscard]] Presentation presentation() const noexcept;
    void togglePip() noexcept;

    void setBrowseSection(BrowseSection section) noexcept;
    [[nodiscard]] BrowseSection browseSection() const noexcept;
    void browseMove(int delta) noexcept;
    [[nodiscard]] std::size_t browseSelection() const noexcept;

    void setVolume(float value) noexcept;
    [[nodiscard]] float volume() const noexcept;

private:
    std::vector<Track> library_;
    std::vector<std::size_t> queue_;
    std::size_t queueIndex_ = 0;
    Screen screen_ = Screen::Hero;
    Presentation presentation_ = Presentation::BigScreen;
    BrowseSection browseSection_ = BrowseSection::Music;
    std::size_t browseSelection_ = 0;
    float volume_ = 0.82f;
};

std::filesystem::path etherPlayProfileDir();
std::wstring displayTitleForPath(const std::wstring& path);

} // namespace etherplayer
