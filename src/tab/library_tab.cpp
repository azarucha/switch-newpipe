#include "tab/library_tab.hpp"

#include "activity/stream_detail_activity.hpp"
#include "activity/stream_feed_activity.hpp"
#include "newpipe/i18n.hpp"
#include "newpipe/library_store.hpp"
#include "newpipe/log.hpp"
#include "newpipe/playback_helper.hpp"
#include "newpipe/runtime.hpp"
#include "newpipe/settings_store.hpp"
#include "newpipe/youtube_resolver.hpp"
#include "view/stream_card.hpp"
#include "view/tab_focus.hpp"

namespace {
constexpr size_t kGridColumns = 3;

const char* section_key(int section) {
    switch (section) {
        case 1:
            return "library/playlists";
        case 2:
            return "library/favorites";
        case 3:
            return "library/downloads";
        default:
            return "library/history";
    }
}
}  // namespace

LibraryTab::LibraryTab() : service_() {
    this->inflateFromXMLRes("xml/tabs/library.xml");
    brls::delay(700, [this]() { interactionReady_.store(true); });

    // Kept on the content only: clearing is destructive and needs no sidebar
    // shortcut, an empty section has nothing to clear anyway.
    this->registerAction(newpipe::tr("library/clear_action"), brls::ControllerButton::BUTTON_RB, [this](brls::View*) {
        this->clearCurrentSection();
        return true;
    });

    this->refresh();
}

void LibraryTab::onCreate() {
    // Mirrored on the sidebar item: an empty section has no focusable child,
    // so a content-only action could never be triggered.
    this->registerTabAction(newpipe::tr("common/refresh"), brls::ControllerButton::BUTTON_X, [this](brls::View*) {
        this->refresh();
        return true;
    });
    this->registerTabAction(newpipe::tr("library/section_action"), brls::ControllerButton::BUTTON_LB, [this](brls::View*) {
        this->toggleSection();
        return true;
    });
}

bool LibraryTab::allowInitialInput() const {
    return interactionReady_.load();
}

void LibraryTab::buildChips() {
    if (!this->chipsBox) {
        return;
    }
    this->chipsBox->clearViews();
    for (int i = 0; i < kSectionCount; ++i) {
        const bool active = i == static_cast<int>(this->section_);
        auto* chip = new brls::Box(brls::Axis::ROW);
        chip->setPadding(7, 16, 7, 16);
        chip->setMarginRight(10);
        chip->setCornerRadius(8);
        chip->setBackgroundColor(active ? nvgRGB(241, 241, 241) : nvgRGB(39, 39, 39));
        auto* label = new brls::Label();
        label->setFontSize(15);
        label->setSingleLine(true);
        label->setText(newpipe::tr(section_key(i)));
        label->setTextColor(active ? nvgRGB(15, 15, 15) : nvgRGB(241, 241, 241));
        chip->addView(label);
        this->chipsBox->addView(chip);
    }
    auto* hint = new brls::Label();
    hint->setFontSize(13);
    hint->setMarginLeft(6);
    hint->setText(newpipe::tr("library/chips_hint"));
    hint->setTextColor(nvgRGB(113, 113, 113));
    this->chipsBox->addView(hint);
}

void LibraryTab::refresh() {
    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::VISIBLE);
    }
    this->buildChips();

    std::string error;
    std::string empty_text;
    newpipe::LibraryStore::instance().load(&error);
    this->items_.clear();
    switch (this->section_) {
        case Section::History:
            this->items_ = newpipe::LibraryStore::instance().history_items();
            empty_text = newpipe::tr("library/history_empty");
            break;
        case Section::Favorites:
            this->items_ = newpipe::LibraryStore::instance().favorite_items();
            empty_text = newpipe::tr("library/favorites_empty");
            break;
        case Section::Playlists: {
            const auto feed = this->service_.get_library_playlists();
            if (feed.has_value()) {
                this->items_ = feed->items;
            }
            empty_text = this->service_.error_message().empty() ? newpipe::tr("library/playlists_empty")
                                                                : this->service_.error_message();
            break;
        }
        case Section::Downloads:
            // Progress is shown in the card's meta line; X refreshes it.
            this->downloads_ = newpipe::DownloadManager::instance().entries();
            for (const auto& entry : this->downloads_) {
                newpipe::StreamItem item = entry.item;
                switch (entry.state) {
                    case newpipe::DownloadState::Done:
                        item.view_count_text = newpipe::tr("library/download_done", entry.height);
                        break;
                    case newpipe::DownloadState::Downloading:
                        item.view_count_text = newpipe::tr("library/download_progress", entry.progress_percent);
                        break;
                    case newpipe::DownloadState::Queued:
                        item.view_count_text = newpipe::tr("library/download_queued");
                        break;
                    case newpipe::DownloadState::Failed:
                        item.view_count_text = newpipe::tr("library/download_failed");
                        break;
                }
                item.published_text.clear();
                this->items_.push_back(item);
            }
            empty_text = newpipe::tr("library/downloads_empty");
            break;
    }
    this->buildGrid();

    if (this->statusLabel) {
        this->statusLabel->setText(newpipe::tr("app/library"));
    }
    if (this->bodyLabel) {
        this->bodyLabel->setText(this->items_.empty() ? empty_text : "");
        this->bodyLabel->setVisibility(this->items_.empty() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }
    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::GONE);
    }
}

void LibraryTab::buildGrid() {
    if (!this->gridBox) {
        return;
    }

    newpipe::release_grid_focus(this, this->gridBox);
    this->gridBox->clearViews();
    for (size_t i = 0; i < this->items_.size(); i += kGridColumns) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(22);

        for (size_t j = i; j < i + kGridColumns && j < this->items_.size(); j++) {
            auto* card = new StreamCard();
            card->setData(this->items_[j]);
            const size_t idx = j;
            if (this->section_ == Section::Downloads) {
                card->registerClickAction([this, idx](brls::View*) {
                    this->openDownload(idx);
                    return true;
                });
                card->registerAction(newpipe::tr("library/download_delete"), brls::ControllerButton::BUTTON_Y, [this, idx](brls::View*) {
                    this->confirmDeleteDownload(idx);
                    return true;
                });
            } else if (this->section_ == Section::Playlists) {
                card->registerClickAction([this, idx](brls::View*) {
                    this->openPlaylist(this->items_[idx]);
                    return true;
                });
            } else {
                card->registerClickAction([this, idx](brls::View*) {
                    this->playStream(this->items_[idx]);
                    return true;
                });
                card->registerAction(newpipe::tr("common/info"), brls::ControllerButton::BUTTON_Y, [this, idx](brls::View*) {
                    this->openStream(this->items_[idx]);
                    return true;
                });
            }
            card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
            row->addView(card);
        }

        this->gridBox->addView(row);
    }
}

// History -> Playlists -> Favorites -> Downloads -> History.
void LibraryTab::toggleSection() {
    this->section_ = static_cast<Section>((static_cast<int>(this->section_) + 1) % kSectionCount);
    this->refresh();
}

void LibraryTab::openPlaylist(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    const auto feed = this->service_.get_full_playlist(item.id);
    if (!feed.has_value()) {
        brls::Application::notify(this->service_.error_message().empty()
                ? newpipe::tr("detail/playlist_load_failed")
                : this->service_.error_message());
        return;
    }
    brls::Application::pushActivity(new StreamFeedActivity(item.title, feed->items, feed->continuation));
}

void LibraryTab::openDownload(size_t index) {
    if (!allowInitialInput() || index >= this->downloads_.size()) {
        return;
    }

    const auto& entry = this->downloads_[index];
    switch (entry.state) {
        case newpipe::DownloadState::Done: {
            newpipe::PlaybackRequest request;
            request.title = entry.item.title;
            request.url = entry.video_path;
            request.local_audio_path = entry.audio_path;
            newpipe::logf("library: play download id=%s", entry.item.id.c_str());
            newpipe::queue_playback(request);
            brls::Application::quit();
            return;
        }
        case newpipe::DownloadState::Failed: {
            const auto quality = newpipe::SettingsStore::instance().settings().playback_quality;
            std::string error;
            if (newpipe::DownloadManager::instance().enqueue(
                    entry.item, newpipe::preferred_height_for_quality(quality), &error)) {
                brls::Application::notify(newpipe::tr("library/download_retry"));
            } else {
                brls::Application::notify(error);
            }
            this->refresh();
            return;
        }
        case newpipe::DownloadState::Downloading:
        case newpipe::DownloadState::Queued:
            brls::Application::notify(newpipe::tr("library/download_not_ready"));
            this->refresh();
            return;
    }
}

void LibraryTab::confirmDeleteDownload(size_t index) {
    if (index >= this->downloads_.size()) {
        return;
    }

    const std::string id = this->downloads_[index].item.id;
    auto* dialog = new brls::Dialog(
        newpipe::tr("library/download_delete_confirm", this->downloads_[index].item.title));
    dialog->addButton(newpipe::tr("library/download_delete"), [this, dialog, id]() {
        dialog->close();
        std::string error;
        if (!newpipe::DownloadManager::instance().remove(id, &error)) {
            brls::Application::notify(error);
        }
        this->refresh();
    });
    dialog->addButton(newpipe::tr("hints/cancel"), [dialog]() { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}

void LibraryTab::clearCurrentSection() {
    if (this->section_ == Section::Downloads) {
        brls::Application::notify(newpipe::tr("library/downloads_clear_hint"));
        return;
    }
    if (this->section_ == Section::Playlists) {
        return;
    }

    const bool favorites = this->section_ == Section::Favorites;
    std::string error;
    const bool ok = favorites ? newpipe::LibraryStore::instance().clear_favorites(&error)
                              : newpipe::LibraryStore::instance().clear_history(&error);
    if (!ok) {
        brls::Application::notify(error.empty() ? newpipe::tr("library/clear_failed") : error);
        return;
    }

    brls::Application::notify(favorites ? newpipe::tr("library/favorites_cleared")
                                        : newpipe::tr("library/history_cleared"));
    this->refresh();
}

void LibraryTab::playStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    const auto detail = this->service_.get_stream_detail(item.url);
    const auto request = newpipe::build_playback_request(item, detail);
    if (!request.has_value()) {
        this->openStream(item);
        return;
    }

    std::string ignored_error;
    newpipe::LibraryStore::instance().add_history(detail.has_value() ? detail->item : item, &ignored_error);
    newpipe::queue_playback(*request);
    brls::Application::quit();
}

void LibraryTab::openStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    brls::Application::pushActivity(new StreamDetailActivity(item));
}
