#include "tab/home_tab.hpp"

#include "activity/stream_detail_activity.hpp"
#include "newpipe/i18n.hpp"
#include "newpipe/library_store.hpp"
#include "newpipe/log.hpp"
#include "newpipe/playback_helper.hpp"
#include "newpipe/runtime.hpp"
#include "newpipe/settings_store.hpp"
#include "view/stream_card.hpp"
#include "view/tab_focus.hpp"

namespace {
constexpr size_t kGridColumns = 3;
}

HomeTab::HomeTab() : service_() {
    this->inflateFromXMLRes("xml/tabs/home.xml");
    newpipe::log_line("home: construct");

    kiosks_ = service_.list_kiosks();
    newpipe::logf("home: kiosks=%zu", kiosks_.size());
    const newpipe::AppSettings settings = newpipe::SettingsStore::instance().settings();
    for (size_t i = 0; i < kiosks_.size(); i++) {
        if (kiosks_[i].id == settings.home_kiosk) {
            kioskIndex_ = i;
            break;
        }
    }

    if (statusLabel) {
        statusLabel->setText(newpipe::tr("home/preparing"));
    }
    brls::delay(700, [this]() {
        interactionReady_.store(true);
        newpipe::log_line("home: interaction ready");
    });
    scheduleLoadHome(250);
}

void HomeTab::onCreate() {
    // Tab actions are mirrored on the sidebar item so they stay usable while the
    // sidebar holds focus, and when the feed is empty and nothing here is focusable.
    this->registerTabAction(newpipe::tr("common/refresh"), brls::ControllerButton::BUTTON_X, [this](brls::View*) {
        loadHome();
        return true;
    });
    this->registerTabAction(newpipe::tr("home/category_action"), brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        cycleKiosk();
        return true;
    });
}

void HomeTab::loadHome() {
    newpipe::logf("home: loadHome index=%zu", kioskIndex_);
    if (statusLabel) {
        statusLabel->setVisibility(brls::Visibility::VISIBLE);
    }
    if (!initialLoadCompleted_) {
        initialLoadAttempts_++;
    }
    if (spinner) {
        spinner->setVisibility(brls::Visibility::VISIBLE);
    }

    if (!service_.is_loaded()) {
        if (statusLabel) {
            statusLabel->setText(newpipe::tr("common/service_init_failed", service_.error_message()));
        }
        if (spinner) {
            spinner->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    if (kiosks_.empty()) {
        if (statusLabel) {
            statusLabel->setText(newpipe::tr("home/no_kiosk"));
        }
        if (spinner) {
            spinner->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    kioskIndex_ %= kiosks_.size();
    buildChips();
    const auto feed = service_.get_home_feed(kiosks_[kioskIndex_].id);
    if (!feed.has_value()) {
        if (statusLabel) {
            if (service_.error_message().empty()) {
                statusLabel->setText(newpipe::tr("home/load_failed"));
            } else {
                statusLabel->setText(service_.error_message());
            }
        }
        if (spinner) {
            spinner->setVisibility(brls::Visibility::GONE);
        }
        if (!initialLoadCompleted_ && items_.empty() && initialLoadAttempts_ < 4) {
            if (statusLabel) {
                statusLabel->setText(newpipe::tr("home/preparing"));
            }
            newpipe::logf("home: auto retry attempt=%d", initialLoadAttempts_);
            scheduleLoadHome(350);
        }
        return;
    }

    initialLoadCompleted_ = true;
    items_ = feed->items;
    newpipe::logf("home: feed=%s items=%zu", feed->kiosk.id.c_str(), items_.size());
    buildGrid();

    if (statusLabel) {
        std::string title = feed->kiosk.title;
        const std::string option_key = "settings/home_kiosk/options/" + feed->kiosk.id;
        const std::string translated = newpipe::tr(option_key);
        if (!translated.empty() && translated != option_key) {
            title = translated;
        }
        statusLabel->setText("");
        statusLabel->setVisibility(brls::Visibility::GONE);
    }
    if (spinner) {
        spinner->setVisibility(brls::Visibility::GONE);
    }
}

void HomeTab::scheduleLoadHome(long delay_ms) {
    brls::delay(delay_ms, [this]() { loadHome(); });
}

void HomeTab::buildGrid() {
    if (!gridBox) {
        newpipe::log_line("home: buildGrid gridBox missing");
        return;
    }

    newpipe::logf("home: buildGrid items=%zu", items_.size());
    newpipe::release_grid_focus(this, gridBox);
    gridBox->clearViews();
    for (size_t i = 0; i < items_.size(); i += kGridColumns) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(22);

        for (size_t j = i; j < i + kGridColumns && j < items_.size(); j++) {
            auto* card = new StreamCard();
            card->setData(items_[j]);
            const size_t idx = j;
            card->registerClickAction([this, idx](brls::View*) {
                playStream(items_[idx]);
                return true;
            });
            card->registerAction(newpipe::tr("common/info"), brls::ControllerButton::BUTTON_Y, [this, idx](brls::View*) {
                openStream(items_[idx]);
                return true;
            });
            card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
            row->addView(card);
        }

        gridBox->addView(row);
    }
}

// YouTube-style category chips; Y moves to the next one.
void HomeTab::buildChips() {
    if (!chipsBox) {
        return;
    }
    chipsBox->clearViews();
    for (size_t i = 0; i < kiosks_.size(); i++) {
        const bool active = i == kioskIndex_;
        auto* chip = new brls::Box(brls::Axis::ROW);
        chip->setPadding(7, 16, 7, 16);
        chip->setMarginRight(10);
        chip->setCornerRadius(8);
        chip->setBackgroundColor(active ? nvgRGB(241, 241, 241) : nvgRGB(39, 39, 39));

        std::string title = kiosks_[i].title;
        const std::string option_key = "settings/home_kiosk/options/" + kiosks_[i].id;
        const std::string translated = newpipe::tr(option_key);
        if (!translated.empty() && translated != option_key) {
            title = translated;
        }
        auto* label = new brls::Label();
        label->setFontSize(15);
        label->setSingleLine(true);
        label->setText(title);
        label->setTextColor(active ? nvgRGB(15, 15, 15) : nvgRGB(241, 241, 241));
        chip->addView(label);
        chipsBox->addView(chip);
    }
    auto* hint = new brls::Label();
    hint->setFontSize(13);
    hint->setMarginLeft(6);
    hint->setText(newpipe::tr("home/chips_hint"));
    hint->setTextColor(nvgRGB(113, 113, 113));
    hint->setVerticalAlign(brls::VerticalAlign::CENTER);
    chipsBox->addView(hint);
}

void HomeTab::cycleKiosk() {
    if (!allowInitialInput()) {
        return;
    }
    if (kiosks_.empty()) {
        return;
    }

    kioskIndex_ = (kioskIndex_ + 1) % kiosks_.size();
    newpipe::logf("home: cycleKiosk newIndex=%zu id=%s", kioskIndex_, kiosks_[kioskIndex_].id.c_str());
    loadHome();
}

bool HomeTab::allowInitialInput() const {
    if (interactionReady_.load()) {
        return true;
    }

    newpipe::log_line("home: ignored startup input");
    return false;
}

void HomeTab::playStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    newpipe::logf("home: playStream url=%s", item.url.c_str());
    const auto detail = service_.get_stream_detail(item.url);
    const auto request = newpipe::build_playback_request(item, detail);
    if (!request.has_value()) {
        openStream(item);
        return;
    }

    std::string ignored_error;
    newpipe::LibraryStore::instance().add_history(detail.has_value() ? detail->item : item, &ignored_error);
    newpipe::logf("home: queue playback url=%s", request->url.c_str());
    newpipe::queue_playback(*request);
    brls::Application::quit();
}

void HomeTab::openStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    newpipe::logf("home: openStream url=%s", item.url.c_str());
    brls::Application::pushActivity(new StreamDetailActivity(item));
}
