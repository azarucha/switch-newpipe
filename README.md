<p align="center">
  <img src="./docs/banner.png" alt="Switch-NewPipe" width="848"/>
</p>

<p align="center">
  <strong>A free, open-source YouTube client for Nintendo Switch homebrew.</strong><br>
  Inspired by <a href="https://github.com/TeamNewPipe/NewPipe">NewPipe</a> &mdash; no Google account required, no ads, no tracking.
</p>

<p align="center">
  <a href="./README_kr.md">한국어</a>
</p>

---

## Screenshots

| Home Feed | Player (720p) |
|:-:|:-:|
| ![home](./docs/preview2.jpg) | ![player](./docs/preview1.jpg) |

## Install

1. Make sure your Switch has **Atmosphere CFW** with the Homebrew Menu
2. Download `switch_newpipe.nro` from the [latest release](../../releases/latest)
3. Copy it to `sdmc:/switch/switch_newpipe.nro`
4. Launch from the Homebrew Menu

## What You Can Do

- Browse **Home**, **Search**, **Subscriptions**, **Library**, and **Settings**
- Watch YouTube up to **1080p** in docked mode (**720p** handheld) — HLS streaming, no throttle
- Search for any video and play it immediately
- Log in with cookies to see your subscriptions and personalized recommendations
- Save watch history and favorites locally
- English & Korean UI

## Controls

### Main UI

| Button | Action |
|--------|--------|
| `A` | Play video from list |
| `Y` | Open video details |
| `X` | Refresh / Reset defaults |
| `RB` | Manage login session (Subscriptions tab) |

### Player

| Button | Action |
|--------|--------|
| `A` | Pause / Resume |
| `B` | Exit player |
| `Up / Down` | Volume |
| `Left / Right` | Seek 10 seconds |
| `LB / RB` | Seek 60 seconds |
| `X / Y` | Toggle OSD overlay |
| `-` (Minus) | Switch quality (Best → 1080p → 720p → 320p) and resume at the same position |

On progressive and UMP streams you can only seek inside the part that has already
been downloaded; the OSD progress bar shows that range and the reachable limit.

## Login (Optional)

**Sign in with a code (recommended):**

1. Open the **Subscriptions** tab and press `RB`
2. Choose **Sign in with code**
3. On your phone or PC, open the shown address (google.com/device) and enter the code

The app signs in through the YouTube TV client, the same way TV apps do. Google has
flagged accounts used with unofficial clients before, so consider the risk for your
main account. The login is saved in `switch_newpipe_session.json` and refreshed automatically.

**Or import cookies:**

1. Export your YouTube cookies from a browser (using a cookie export extension)
2. Save the file as `sdmc:/switch/switch_newpipe_auth.txt`
3. Restart the app

Supported formats: raw `Cookie` header, JSON `{"cookie_header":"..."}`, or Netscape `cookies.txt`.

Once logged in, your **Subscriptions** tab and **personalized Home recommendations** will be available.

## Downloads

Open a video, press `LB` and choose **Download**. The video is saved in the quality
set under Settings (video and audio as two files in `sdmc:/switch/switch_newpipe_downloads/`)
and keeps downloading in the background, also while you watch something else.

Find it under **Library**: `LB` switches History → Favorites → Downloads. `A` plays a
finished download offline, `Y` deletes it, `X` refreshes the progress. A download cut
off by closing the app shows as failed; `A` starts it again.

## Playback Quality

Configure in **Settings** tab:

| Mode | Description |
|------|-------------|
| **Best** | Auto by console state: **1080p when docked**, **720p in handheld** |
| **1080p** | Always target 1080p (HLS), falls back gracefully |
| **720p** | Always target 720p (HLS), falls back gracefully |
| **320p** | Low quality (~360p progressive MP4) to save bandwidth |

## Data Files

All data is stored on your SD card:

| File | Purpose |
|------|---------|
| `sdmc:/switch/switch_newpipe.log` | Debug log |
| `sdmc:/switch/switch_newpipe_settings.json` | Settings |
| `sdmc:/switch/switch_newpipe_library.json` | Watch history & favorites |
| `sdmc:/switch/switch_newpipe_session.json` | Login session |
| `sdmc:/switch/switch_newpipe_auth.txt` | Cookie import (you provide this) |

## Build from Source

Requires Docker and a host C++ compiler.

```bash
# Full build (portlibs + app)
./build.sh

# App only (after first full build)
./build.sh --app-only

# Clean everything
./build.sh --clean
```

Output: `cmake-build-switch/switch_newpipe.nro`

<details>
<summary>Host validation tools (for development)</summary>

```bash
make host
./build/host/switch_newpipe_host
./build/host/switch_newpipe_host --search Zelda
./build/host/switch_newpipe_host --resolve 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
```

</details>

## Known Limitations

- Seek on progressive / UMP streams only reaches the already downloaded part
- A quality change on progressive / UMP streams restarts from the beginning
- Channel pages are not fully browsable yet

## Tech Stack

- **UI**: [Borealis](https://github.com/natinusala/borealis) (native Switch UI framework)
- **Playback**: mpv + FFmpeg (hardware-accelerated on Switch)
- **Networking**: libcurl + custom YouTube innertube API client
- **Build**: CMake, Docker, devkitPro toolchain

## License

This project is for educational purposes. It is not affiliated with YouTube, Google, or NewPipe.
