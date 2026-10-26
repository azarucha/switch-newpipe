#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "newpipe/models.hpp"

namespace newpipe {

enum class DownloadState { Queued, Downloading, Done, Failed };

struct DownloadEntry {
    StreamItem item;
    DownloadState state = DownloadState::Queued;
    int progress_percent = 0;
    int height = 0;
    std::string video_path;
    std::string audio_path;  // empty when the stream had no separate audio
    std::string error;
};

std::string default_downloads_index_path();
std::string default_downloads_dir();

// Downloads YouTube videos (HLS, video and audio as separate MPEG-TS files)
// to the SD card on one background thread. Keeps running while the player is
// open; a download interrupted by quitting the app comes back as Failed.
class DownloadManager {
public:
    static DownloadManager& instance();

    bool enqueue(const StreamItem& item, int preferred_height, std::string* error_message = nullptr);
    bool remove(const std::string& id, std::string* error_message = nullptr);
    std::vector<DownloadEntry> entries() const;
    std::optional<DownloadEntry> find(const std::string& id) const;

private:
    DownloadManager();

    void load_index();
    void persist_index_locked() const;
    void ensure_worker_locked();
    void worker();
    bool download_one(const std::string& id, int preferred_height, std::string& error);
    void set_progress(const std::string& id, int percent);

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<DownloadEntry> entries_;
    std::deque<std::pair<std::string, int>> queue_;  // id, preferred height
    std::string active_id_;
    std::atomic<bool> abort_active_{false};
    bool worker_started_ = false;
};

}  // namespace newpipe
