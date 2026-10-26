#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/youtube_catalog_service.hpp"
#include "view/auto_tab_frame.hpp"

class SubscriptionsTab : public AttachedView {
public:
    SubscriptionsTab();
    ~SubscriptionsTab() override;

    void onCreate() override;

    static brls::View* create() { return new SubscriptionsTab(); }

private:
    void refresh();
    void buildGrid();
    void clearGrid();
    void showSignedOutState();
    void openSessionDialog();
    void handleManualCookieInput(const std::string& text);
    void startDeviceLogin();
    bool allowInitialInput() const;
    void playStream(const newpipe::StreamItem& item);
    void openStream(const newpipe::StreamItem& item);

    BRLS_BIND(brls::Label, statusLabel, "subscriptions/status");
    BRLS_BIND(brls::Label, bodyLabel, "subscriptions/body");
    BRLS_BIND(brls::ProgressSpinner, spinner, "subscriptions/spinner");
    BRLS_BIND(brls::ScrollingFrame, scrollFrame, "subscriptions/scroll");
    BRLS_BIND(brls::Box, gridBox, "subscriptions/grid");

    newpipe::YouTubeCatalogService service_;
    std::vector<newpipe::StreamItem> items_;
    std::atomic<bool> interactionReady_{false};
    // Shared with the device-login polling thread; set when the tab goes away
    // or the login dialog is dismissed.
    std::shared_ptr<std::atomic<bool>> deviceLoginCancelled_;
};
