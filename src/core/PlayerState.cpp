#include "etherplayer/PlayerState.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace etherplayer {
namespace {

std::wstring safeProfileName(std::wstring value) {
    if (value.empty()) value = L"local";
    for (auto& ch : value) {
        if (!(std::iswalnum(ch) || ch == L'-' || ch == L'_')) ch = L'_';
    }
    return value;
}

std::string utf8(const std::wstring& value) {
#ifdef _WIN32
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(count, 0)), '\0');
    if (count > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
#else
    return std::string(value.begin(), value.end());
#endif
}

std::wstring wutf8(const std::string& value) {
#ifdef _WIN32
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(count, 0)), L'\0');
    if (count > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

std::map<std::string, std::string> readKv(const fs::path& path) {
    std::map<std::string, std::string> result;
    std::ifstream file(path, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        const auto at = line.find('=');
        if (at != std::string::npos) result[line.substr(0, at)] = line.substr(at + 1);
    }
    return result;
}

Track trackFromPath(const std::wstring& input) {
    Track track;
    std::error_code ec;
    fs::path path = fs::weakly_canonical(fs::path(input), ec);
    if (ec) path = fs::path(input).lexically_normal();
    track.path = path.wstring();
    track.title = path.stem().wstring();
    track.artist = L"unknown artist";

    const auto meta = readKv(fs::path(track.path + L".ethermeta"));
    if (const auto it = meta.find("title"); it != meta.end() && !it->second.empty()) track.title = wutf8(it->second);
    if (const auto it = meta.find("artist"); it != meta.end() && !it->second.empty()) track.artist = wutf8(it->second);
    return track;
}

} // namespace

fs::path etherPlayProfileDir() {
#ifdef _WIN32
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    const wchar_t* user = _wgetenv(L"USERNAME");
    const fs::path root = (local && *local) ? fs::path(local) : fs::temp_directory_path();
    return root / L"EtherPlay" / L"profiles" / safeProfileName(user && *user ? user : L"local");
#else
    return fs::temp_directory_path() / "EtherPlay" / "profiles" / "local";
#endif
}

std::wstring displayTitleForPath(const std::wstring& path) {
    return fs::path(path).stem().wstring();
}

void PlayerState::loadEtherPlayLibrary() {
    std::vector<Track> tracks;
    std::ifstream file(etherPlayProfileDir() / L"library.txt", std::ios::binary);
    std::string line;
    while (std::getline(file, line) && tracks.size() < 1000) {
        if (line.empty()) continue;
        Track track = trackFromPath(wutf8(line));
        std::error_code ec;
        if (!track.path.empty() && fs::exists(track.path, ec)) tracks.push_back(std::move(track));
    }
    setLibrary(std::move(tracks));
}

void PlayerState::setLibrary(std::vector<Track> tracks) {
    library_ = std::move(tracks);
    queue_.clear();
    queueIndex_ = 0;
    browseSelection_ = 0;
}

const std::vector<Track>& PlayerState::library() const noexcept { return library_; }
const std::vector<std::size_t>& PlayerState::queue() const noexcept { return queue_; }

const Track* PlayerState::currentTrack() const noexcept {
    if (queue_.empty() || queueIndex_ >= queue_.size()) return nullptr;
    const std::size_t index = queue_[queueIndex_];
    return index < library_.size() ? &library_[index] : nullptr;
}

bool PlayerState::selectLibraryTrack(std::size_t libraryIndex, bool enqueueRest) {
    if (libraryIndex >= library_.size()) return false;
    queue_.clear();
    queue_.push_back(libraryIndex);
    if (enqueueRest) {
        for (std::size_t i = libraryIndex + 1; i < library_.size(); ++i) queue_.push_back(i);
    }
    queueIndex_ = 0;
    return true;
}

bool PlayerState::playQueueIndex(std::size_t queueIndex) {
    if (queueIndex >= queue_.size()) return false;
    queueIndex_ = queueIndex;
    return true;
}

bool PlayerState::next() {
    if (queueIndex_ + 1 >= queue_.size()) return false;
    ++queueIndex_;
    return true;
}

bool PlayerState::previous() {
    if (queue_.empty() || queueIndex_ == 0) return false;
    --queueIndex_;
    return true;
}

void PlayerState::playNext(std::size_t libraryIndex) {
    if (libraryIndex >= library_.size()) return;
    if (queue_.empty()) {
        queue_.push_back(libraryIndex);
        queueIndex_ = 0;
        return;
    }
    queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(std::min(queueIndex_ + 1, queue_.size())), libraryIndex);
}

void PlayerState::addToQueue(std::size_t libraryIndex) {
    if (libraryIndex < library_.size()) queue_.push_back(libraryIndex);
}

void PlayerState::clearQueue() {
    queue_.clear();
    queueIndex_ = 0;
}

void PlayerState::setScreen(Screen screen) noexcept { screen_ = screen; }
Screen PlayerState::screen() const noexcept { return screen_; }

void PlayerState::setPresentation(Presentation presentation) noexcept { presentation_ = presentation; }
Presentation PlayerState::presentation() const noexcept { return presentation_; }
void PlayerState::togglePip() noexcept {
    presentation_ = presentation_ == Presentation::BigScreen ? Presentation::Pip : Presentation::BigScreen;
}

void PlayerState::setBrowseSection(BrowseSection section) noexcept {
    browseSection_ = section;
    browseSelection_ = 0;
}
BrowseSection PlayerState::browseSection() const noexcept { return browseSection_; }

void PlayerState::browseMove(int delta) noexcept {
    const std::size_t count = browseSection_ == BrowseSection::Songs ? library_.size() : 6;
    if (count == 0) {
        browseSelection_ = 0;
        return;
    }
    long long value = static_cast<long long>(browseSelection_) + delta;
    while (value < 0) value += static_cast<long long>(count);
    browseSelection_ = static_cast<std::size_t>(value) % count;
}

std::size_t PlayerState::browseSelection() const noexcept { return browseSelection_; }

void PlayerState::setVolume(float value) noexcept { volume_ = std::clamp(value, 0.0f, 1.0f); }
float PlayerState::volume() const noexcept { return volume_; }

} // namespace etherplayer
