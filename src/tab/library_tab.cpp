#include "tab/library_tab.hpp"

#include "activity/stream_detail_activity.hpp"
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
constexpr size_t kGridColumns = 4;
}

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
    // Mirrored on the sidebar item: an empty history/favorites list has no
    // focusable child, so a content-only action could never be triggered.
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

void LibraryTab::refresh() {
    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::VISIBLE);
    }

    std::string error;
    newpipe::LibraryStore::instance().load(&error);
    if (this->showingDownloads_) {
        // Progress is shown in the card's meta line; X refreshes it.
        this->downloads_ = newpipe::DownloadManager::instance().entries();
        this->items_.clear();
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
    } else {
        this->items_ = this->showingFavorites_ ? newpipe::LibraryStore::instance().favorite_items()
                                               : newpipe::LibraryStore::instance().history_items();
    }
    this->buildGrid();

    std::string section_title = newpipe::tr("library/history");
    std::string empty_text = newpipe::tr("library/history_empty");
    std::string controls = newpipe::tr("library/controls");
    if (this->showingDownloads_) {
        section_title = newpipe::tr("library/downloads");
        empty_text = newpipe::tr("library/downloads_empty");
        controls = newpipe::tr("library/downloads_controls");
    } else if (this->showingFavorites_) {
        section_title = newpipe::tr("library/favorites");
        empty_text = newpipe::tr("library/favorites_empty");
    }
    if (this->statusLabel) {
        this->statusLabel->setText(
            newpipe::tr("common/count_with_title", section_title, this->items_.size()));
    }
    if (this->bodyLabel) {
        this->bodyLabel->setText(this->items_.empty() ? empty_text : controls);
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
        row->setMarginBottom(8);

        for (size_t j = i; j < i + kGridColumns && j < this->items_.size(); j++) {
            auto* card = new StreamCard();
            card->setData(this->items_[j]);
            const size_t idx = j;
            if (this->showingDownloads_) {
                card->registerClickAction([this, idx](brls::View*) {
                    this->openDownload(idx);
                    return true;
                });
                card->registerAction(newpipe::tr("library/download_delete"), brls::ControllerButton::BUTTON_Y, [this, idx](brls::View*) {
                    this->confirmDeleteDownload(idx);
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

// History -> Favorites -> Downloads -> History.
void LibraryTab::toggleSection() {
    if (this->showingDownloads_) {
        this->showingDownloads_ = false;
    } else if (this->showingFavorites_) {
        this->showingFavorites_ = false;
        this->showingDownloads_ = true;
    } else {
        this->showingFavorites_ = true;
    }
    this->refresh();
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
    dialog->addButton(newpipe::tr("common/cancel"), [dialog]() { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}

void LibraryTab::clearCurrentSection() {
    if (this->showingDownloads_) {
        brls::Application::notify(newpipe::tr("library/downloads_clear_hint"));
        return;
    }

    std::string error;
    const bool ok = this->showingFavorites_ ? newpipe::LibraryStore::instance().clear_favorites(&error)
                                            : newpipe::LibraryStore::instance().clear_history(&error);
    if (!ok) {
        brls::Application::notify(error.empty() ? newpipe::tr("library/clear_failed") : error);
        return;
    }

    brls::Application::notify(this->showingFavorites_ ? newpipe::tr("library/favorites_cleared")
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
