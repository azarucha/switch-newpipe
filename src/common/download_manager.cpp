#include "newpipe/download_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <thread>

#include <sys/stat.h>

#include "nlohmann/json.hpp"
#include "newpipe/http_client.hpp"
#include "newpipe/log.hpp"
#include "newpipe/youtube_resolver.hpp"

namespace newpipe {
namespace {

using nlohmann::json;

constexpr int kSegmentRetries = 3;

const char* state_name(DownloadState state) {
    switch (state) {
        case DownloadState::Queued:
            return "queued";
        case DownloadState::Downloading:
            return "downloading";
        case DownloadState::Done:
            return "done";
        case DownloadState::Failed:
            return "failed";
    }
    return "failed";
}

DownloadState state_from_name(const std::string& name) {
    if (name == "done") {
        return DownloadState::Done;
    }
    // Queued / downloading entries were interrupted by quitting the app.
    return DownloadState::Failed;
}

json item_to_json(const StreamItem& item) {
    return {
        {"id", item.id},
        {"url", item.url},
        {"title", item.title},
        {"channel_name", item.channel_name},
        {"channel_url", item.channel_url},
        {"channel_id", item.channel_id},
        {"thumbnail_url", item.thumbnail_url},
        {"duration_text", item.duration_text},
    };
}

StreamItem item_from_json(const json& node) {
    StreamItem item;
    item.id = node.value("id", std::string());
    item.url = node.value("url", std::string());
    item.title = node.value("title", std::string());
    item.channel_name = node.value("channel_name", std::string());
    item.channel_url = node.value("channel_url", std::string());
    item.channel_id = node.value("channel_id", std::string());
    item.thumbnail_url = node.value("thumbnail_url", std::string());
    item.duration_text = node.value("duration_text", std::string());
    return item;
}

// Appends every segment to one file; MPEG-TS segments concatenate into a
// playable stream. Returns false on a failed segment or when aborted.
bool download_segments(
    HttpClient& client,
    const std::vector<std::string>& segments,
    const std::string& path,
    const std::atomic<bool>& abort,
    const std::function<void(size_t)>& on_segment_done,
    std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        error = "cannot write " + path;
        return false;
    }

    for (size_t i = 0; i < segments.size(); ++i) {
        if (abort.load()) {
            error = "aborted";
            return false;
        }

        std::optional<std::string> body;
        for (int attempt = 0; attempt < kSegmentRetries && !body.has_value(); ++attempt) {
            body = client.get(segments[i]);
            if (body.has_value() && body->empty()) {
                body.reset();
            }
        }
        if (!body.has_value()) {
            error = "segment " + std::to_string(i + 1) + "/" + std::to_string(segments.size()) + " failed";
            return false;
        }

        output.write(body->data(), static_cast<std::streamsize>(body->size()));
        if (!output.good()) {
            error = "SD card write failed";
            return false;
        }
        on_segment_done(i + 1);
    }
    return true;
}

}  // namespace

std::string default_downloads_index_path() {
#ifdef __SWITCH__
    return "sdmc:/switch/switch_newpipe_downloads.json";
#else
    return "switch_newpipe_downloads.json";
#endif
}

std::string default_downloads_dir() {
#ifdef __SWITCH__
    return "sdmc:/switch/switch_newpipe_downloads";
#else
    return "switch_newpipe_downloads";
#endif
}

DownloadManager& DownloadManager::instance() {
    static DownloadManager manager;
    return manager;
}

DownloadManager::DownloadManager() {
    this->load_index();
}

void DownloadManager::load_index() {
    std::ifstream input(default_downloads_index_path(), std::ios::binary);
    if (!input.good()) {
        return;
    }

    const std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const json root = json::parse(raw, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        log_line("downloads: index parse failed");
        return;
    }

    for (const auto& node : root.value("downloads", json::array())) {
        DownloadEntry entry;
        entry.item = item_from_json(node.value("item", json::object()));
        entry.state = state_from_name(node.value("state", std::string()));
        entry.progress_percent = entry.state == DownloadState::Done ? 100 : 0;
        entry.height = node.value("height", 0);
        entry.video_path = node.value("video_path", std::string());
        entry.audio_path = node.value("audio_path", std::string());
        entry.error = entry.state == DownloadState::Done ? std::string() : std::string("interrupted");
        if (!entry.item.id.empty()) {
            this->entries_.push_back(std::move(entry));
        }
    }
    logf("downloads: loaded %zu entries", this->entries_.size());
}

void DownloadManager::persist_index_locked() const {
    json list = json::array();
    for (const auto& entry : this->entries_) {
        list.push_back({
            {"item", item_to_json(entry.item)},
            {"state", state_name(entry.state)},
            {"height", entry.height},
            {"video_path", entry.video_path},
            {"audio_path", entry.audio_path},
        });
    }

    std::ofstream output(default_downloads_index_path(), std::ios::binary | std::ios::trunc);
    const std::string body = json{{"downloads", list}}.dump(2);
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!output.good()) {
        log_line("downloads: index write failed");
    }
}

bool DownloadManager::enqueue(const StreamItem& item, int preferred_height, std::string* error_message) {
    if (item.id.empty() || item.url.empty()) {
        if (error_message) {
            *error_message = "missing video id";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(this->mutex_);
    auto it = std::find_if(this->entries_.begin(), this->entries_.end(), [&](const DownloadEntry& entry) {
        return entry.item.id == item.id;
    });
    if (it != this->entries_.end()) {
        if (it->state == DownloadState::Queued || it->state == DownloadState::Downloading) {
            if (error_message) {
                error_message->clear();
            }
            return true;
        }
        this->entries_.erase(it);
    }

    DownloadEntry entry;
    entry.item = item;
    entry.state = DownloadState::Queued;
    this->entries_.insert(this->entries_.begin(), entry);
    this->queue_.emplace_back(item.id, preferred_height);
    this->persist_index_locked();
    this->ensure_worker_locked();
    this->wake_.notify_one();
    logf("downloads: queued id=%s height=%d", item.id.c_str(), preferred_height);
    if (error_message) {
        error_message->clear();
    }
    return true;
}

bool DownloadManager::remove(const std::string& id, std::string* error_message) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    auto it = std::find_if(this->entries_.begin(), this->entries_.end(), [&](const DownloadEntry& entry) {
        return entry.item.id == id;
    });
    if (it == this->entries_.end()) {
        if (error_message) {
            *error_message = "download not found";
        }
        return false;
    }

    if (id == this->active_id_) {
        // The worker deletes the partial files once it sees the abort.
        this->abort_active_.store(true);
    } else {
        std::remove(it->video_path.c_str());
        if (!it->audio_path.empty()) {
            std::remove(it->audio_path.c_str());
        }
    }
    this->queue_.erase(
        std::remove_if(this->queue_.begin(), this->queue_.end(), [&](const auto& queued) {
            return queued.first == id;
        }),
        this->queue_.end());
    this->entries_.erase(it);
    this->persist_index_locked();
    logf("downloads: removed id=%s", id.c_str());
    if (error_message) {
        error_message->clear();
    }
    return true;
}

std::vector<DownloadEntry> DownloadManager::entries() const {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->entries_;
}

std::optional<DownloadEntry> DownloadManager::find(const std::string& id) const {
    std::lock_guard<std::mutex> lock(this->mutex_);
    for (const auto& entry : this->entries_) {
        if (entry.item.id == id) {
            return entry;
        }
    }
    return std::nullopt;
}

void DownloadManager::ensure_worker_locked() {
    if (this->worker_started_) {
        return;
    }
    this->worker_started_ = true;
    mkdir(default_downloads_dir().c_str(), 0777);
    // Detached: the process may exit with a download in flight.
    std::thread([this]() { this->worker(); }).detach();
}

void DownloadManager::set_progress(const std::string& id, int percent) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    for (auto& entry : this->entries_) {
        if (entry.item.id == id) {
            entry.progress_percent = percent;
            return;
        }
    }
}

void DownloadManager::worker() {
    while (true) {
        std::pair<std::string, int> job;
        {
            std::unique_lock<std::mutex> lock(this->mutex_);
            this->wake_.wait(lock, [this]() { return !this->queue_.empty(); });
            job = this->queue_.front();
            this->queue_.pop_front();
            this->active_id_ = job.first;
            this->abort_active_.store(false);
            for (auto& entry : this->entries_) {
                if (entry.item.id == job.first) {
                    entry.state = DownloadState::Downloading;
                    entry.progress_percent = 0;
                }
            }
        }

        std::string error;
        const bool ok = this->download_one(job.first, job.second, error);

        std::lock_guard<std::mutex> lock(this->mutex_);
        const bool removed = this->abort_active_.load();
        this->active_id_.clear();
        for (auto& entry : this->entries_) {
            if (entry.item.id != job.first) {
                continue;
            }
            entry.state = ok ? DownloadState::Done : DownloadState::Failed;
            entry.progress_percent = ok ? 100 : entry.progress_percent;
            entry.error = ok ? std::string() : error;
        }
        if (!removed) {
            this->persist_index_locked();
        }
        logf("downloads: finished id=%s ok=%d error=%s", job.first.c_str(), ok ? 1 : 0, error.c_str());
    }
}

bool DownloadManager::download_one(const std::string& id, int preferred_height, std::string& error) {
    std::string url;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        for (const auto& entry : this->entries_) {
            if (entry.item.id == id) {
                url = entry.item.url;
            }
        }
    }
    if (url.empty()) {
        error = "removed";
        return false;
    }

    HttpsHttpClient client;
    YouTubeResolver resolver(&client);
    const auto source = resolver.resolve_download(url, preferred_height, error);
    if (!source.has_value()) {
        return false;
    }

    const std::string base = default_downloads_dir() + "/" + id;
    const std::string video_path = base + ".video.ts";
    const std::string audio_path = source->audio_segments.empty() ? std::string() : base + ".audio.ts";
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        for (auto& entry : this->entries_) {
            if (entry.item.id == id) {
                entry.height = source->height;
                entry.video_path = video_path;
                entry.audio_path = audio_path;
            }
        }
    }

    const size_t total = source->video_segments.size() + source->audio_segments.size();
    size_t done_before = 0;
    const auto report = [&](size_t done_in_part) {
        this->set_progress(id, static_cast<int>((done_before + done_in_part) * 100 / total));
    };

    bool ok = download_segments(client, source->video_segments, video_path, this->abort_active_, report, error);
    done_before = source->video_segments.size();
    if (ok && !audio_path.empty()) {
        ok = download_segments(client, source->audio_segments, audio_path, this->abort_active_, report, error);
    }

    if (!ok) {
        std::remove(video_path.c_str());
        if (!audio_path.empty()) {
            std::remove(audio_path.c_str());
        }
    }
    return ok;
}

}  // namespace newpipe
