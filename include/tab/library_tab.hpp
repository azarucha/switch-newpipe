#pragma once

#include <atomic>
#include <vector>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/youtube_catalog_service.hpp"
#include "newpipe/download_manager.hpp"
#include "view/auto_tab_frame.hpp"

class LibraryTab : public AttachedView {
public:
    LibraryTab();

    void onCreate() override;

    static brls::View* create() { return new LibraryTab(); }

private:
    enum class Section { History = 0, Playlists, Favorites, Downloads };
    static constexpr int kSectionCount = 4;

    void refresh();
    void buildGrid();
    void buildChips();
    void toggleSection();
    void clearCurrentSection();
    bool allowInitialInput() const;
    void playStream(const newpipe::StreamItem& item);
    void openStream(const newpipe::StreamItem& item);
    void openPlaylist(const newpipe::StreamItem& item);
    void openDownload(size_t index);
    void confirmDeleteDownload(size_t index);

    BRLS_BIND(brls::Label, statusLabel, "library/status");
    BRLS_BIND(brls::Label, bodyLabel, "library/body");
    BRLS_BIND(brls::Box, chipsBox, "library/chips");
    BRLS_BIND(brls::ProgressSpinner, spinner, "library/spinner");
    BRLS_BIND(brls::ScrollingFrame, scrollFrame, "library/scroll");
    BRLS_BIND(brls::Box, gridBox, "library/grid");

    newpipe::YouTubeCatalogService service_;
    std::vector<newpipe::StreamItem> items_;
    std::vector<newpipe::DownloadEntry> downloads_;
    Section section_ = Section::History;
    std::atomic<bool> interactionReady_{false};
};
