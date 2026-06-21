# SkyCompass Satellite (Spacecraft Observation Compass)

[简体中文](README.md) | **English**

> **🔥 Firmware is released on M5Burner! You can directly search for `SkyCompass Satellite` in the official M5Burner burning tool and flash it with a single click (no compilation required).**

![SkyCompass Satellite Cover](cover.jpg)

## Project Overview

**SkyCompass Satellite** is an extended and evolved version of the SkyCompass project, designed to run on the **M5Stack Cardputer ADV (ESP32-S3)**. Unlike the original project which focuses on natural celestial bodies (Sun, Moon, etc.), the Satellite version specializes in **real-time tracking and visibility predictions for human-made spacecrafts (such as the ISS, Tiangong Space Station, Hubble Space Telescope, etc.)**.

**Core Product Value:** Answering the key question: **"Which spacecraft are worth observing tonight?"**. It is not just an orbital trajectory plotter, but a smart recommender system telling you exactly when and where to look up to witness satellite passes.

## Hardware Support

- M5 Cardputer ADV (ESP32-S3)
- Built-in 6-Axis IMU (Accelerometer + Gyroscope)
- Built-in GNSS Module (for positioning and high-precision UTC time synchronization)
- Physical Keyboard + Miniature TFT Screen

![Hardware Connection Diagram](docs/schematic_diagram.png)

## Core Features

- **Tactile Panoramic Interaction & Key Mappings**:
  - `Enter`: Slide out/in the recommended observation passes list panel.
  - `S`: Open the Satellite selection menu to select which satellites to draw on the screen. Press `d` in the menu to delete custom-downloaded satellites.
  - `W`: Enable/disable WiFi to perform NTP time synchronization and TLE data update.
  - `H`: Slide out the **Keyboard Shortcuts Help Menu** at the center of the screen.
  - `C`: Enter/exit **Manual Location Mode (Crosshair Mode)**, using `;` (up), `.` (down), `,` (left), and `/` (right) like a joystick to rotate the globe and manually designate any observer location on Earth.
  - `G`: Enable/disable **GNSS Forced Always-On Mode**, bypassing the auto-sleep power management to manually acquire GPS coordinates.
  - `V`: Enter/exit **Spacecraft Follow Mode (Sat View Mode)**. In this mode, press `;` (previous) and `.` (next) to cycle focus between selected satellites.
  - `Space`: Lock/unlock **IMU Camera Angle**. In Sat View Mode, pressing Space locks the current observation viewpoint; releasing it snaps the camera back to sync with physical device tilt.
  - `Del` (Backspace): Toggle **HUD Data Overlay** (toggles latitude/longitude overlays at corners).
  - `[` and `]`: Adjust TFT screen brightness (from 16 to 255).
- **Live Mode & Time Machine (Simulated Time Travel)**:
  - Vector 3D Earth rendering with **dynamic cold/warm gradient continent outlines** and daylight/shadow terminator.
  - **Visual Anchors**: Automatically computes and floats a "dynamic compass" at the screen corner; projects a "3D axis & polar crosshair grid" at polar zones to resolve disorientation when looking from an orbital perspective.
  - Press **, (backward)** or **/ (forward)** to toggle the **Time Machine**, supporting long-press for high-speed time travel to preview future passes.
  - **NASA Nightlights & Light Pollution Overlay**: Automatically imports global nightlight points from NASA's VIIRS Black Marble dataset. Using importance sampling, it renders the **2000 brightest global light pollution spots** at 30 FPS. Faded dynamically at the dark hemisphere with a smooth dusk/dawn transition (glows at sunset, full brightness at night, fades out at dawn).
- **Smart Observation Recommender (Core Value)**:
  - Slide out the left panel by pressing **`Enter`**.
  - Computes Earth's shadow, solar angle, and dynamic **Visual Magnitude (brightness)** prediction to recommend the best "Visible Windows" (AOS/LOS time, peak elevation, and star scores).
- **Lossless Screen Streaming & Serialization**:
  - Solves the Cardputer's **lack of PSRAM** (which causes PNG compression library to trigger Out-Of-Memory crashes).
  - Pressing the side button (BtnA / GPIO0) dumps the 64KB raw RGB565 frame bytes encoded in Base64 via Serial, marked with the magic prefix `==SKYCOMPASS_DATA==`.
  - A PC-side Python script (`scripts/get_screenshot.py`) intercepts the log channel, filters garbage logs and ANSI colors, and reconstructs the data stream into a lossless 24-bit BMP screenshot.
- **Standalone Offline Survival Capability**:
  - Press **`W`** to scan local WiFi networks and input passwords directly on the Cardputer keyboard.
  - Automatically fetches the latest Celestrak TLEs and caches them in **LittleFS** (with a 48-hour expiration lifecycle).
  - Automatically parses GNSS NMEA strings to update observer coordinates and synchronize the hardware RTC clock to atomic UTC time.
- **Aggressive Power Management**:
  - Shuts down the WiFi RF transceiver immediately after downloading TLEs.
  - Sends deep sleep commands (e.g., `$PCAS10,0*1C`) to the GNSS chip and releases the serial port once a 3D Lock is achieved or after a 1-minute timeout.
- **Cloud Frequency Synchronization (GitHub Action API Gateway)**:
  - Automatically runs a GitHub workflow `.github/workflows/update_frequencies.yml` to filter the massive official frequencies list into a lightweight `data/frequencies.json` file.
  - The Cardputer pulls this lightweight JSON when online to display the real-time downlink frequencies and modulation modes (e.g., ISS SSTV, NOAA APT) for Amateur Radio (HAM)通联 (contacts).

## Relation to Original SkyCompass

**Shared Foundation:**
- Time systems and GNSS NMEA parsing.
- Coordinate transformation system (ECEF/LatLon/Alt -> Az/Alt).
- Low-level UI rendering and canvas libraries.
- Astronomical models (Sun Calculator).

**Functional Differences:**
| Feature | Original SkyCompass | SkyCompass Satellite |
| ---- | ---------- | ---------- |
| Targets | Natural bodies (Sun, Moon, Milky Way) | Human spacecraft (Satellites, Space Stations) |
| Orbit Speed | Slow periodic changes | Ultra-fast velocity (~90 min to orbit Earth) |
| Core UX | Spatial pointing & 3D stellar observation | Pass predictions & visible window planner |
| Data Source | Astronomical Ephemeris formulas | TLE + SGP4 propagation model |

## Architecture & Roadmap

- `[x]` **Phase 0 (Verification)**: Ported SGP4 propagation model, parsing TLEs and solving ECEF coordinates.
- `[x]` **Phase 1 (MVP Globe)**: Plotted 3D globe and orbit mapping, running closed-loop orbit calculations on screen.
- `[x]` **Phase 2 (Day/Night Terminator)**: Implemented SunCalculator to solve shadow/eclipsed states.
- `[x]` **Phase 3 (Recommender System)**: Developed rule-engine rankings based on peak elevation and visual magnitude.
- `[x]` **Phase 4 (WiFi & Storage)**: Implemented WiFi scanner UI, LittleFS cache storage, and GeoIP city lookup.
- `[x]` **Phase 5 (Interaction Upgrades)**: Added TLE multi-select list, spring-mass anti-overlapping UI labels, and Time Machine travel.
- `[x]` **Phase 6 (Amateur Radio Frequencies)**: Integrated real-time uplink/downlink frequencies (ISS SSTV, NOAA APT) for field SDR tuning.
- `[x]` **Phase 7 (Nightlights & Lossless Screen Stream)**: Embedded 2000 NASA nightlight dots at 30 FPS and lossless RGB565 serial screenshot receiver.

## Future Outlook (TODO List)

- `[ ]` **Night Vision Mode**: One-click red-tinted screen filter to preserve dark adaptation for outdoor observations.
- `[x]` **Visual Magnitude Modeling**: Predict brightness based on satellite RCS and solar phase angle.
- `[x]` **HAM Radio Downlinks**: Display live transceiver tuning parameters.
- `[ ]` **Starlink Train Tracker**: Track and predict spectacular orbital trains from recently launched Starlink batches.
- `[ ]` **Countdowns & Buzzer Alerts**: Audible alarms triggered by the buzzer a few minutes prior to AOS.
- `[ ]` **AR Sat Pointer**: Utilizing Cardputer's IMU to render a crosshair, guiding the user to point the device physically towards the skyward satellite location.
- `[ ]` **Deep Sleep Scheduling**: Calculate the next AOS time and put the ESP32 into deep sleep, scheduling an RTC timer to wake it up right before the pass.
- `[x]` **Celestial Background Stars**: Render bright reference stars (e.g., Sirius) on the 3D globe background.
- `[ ]` **Local LAN WebServer**: Host a lightweight web server on the ESP32 to export full 7-day pass timetables to mobile browsers.

## Screenshot Guide

The project uses a zero-memory, uncompressed RGB565 serial stream screenshot system to bypass the lack of PSRAM on the Cardputer.

### 1. Device Trigger
- On any screen, press the physical **BtnA button (GPIO0, located on the side right above the reset switch)**.
- The TFT screen will overlay a yellow `Capturing screen...` warning, and dump the Base64-encoded frame stream to the serial output.

### 2. PC Listener Setup
To intercept and save screenshots on your PC:

1. **Install Python dependencies**:
   ```bash
   pip install pyserial Pillow
   ```
2. **Launch the listener**:
   Run this script from the project root directory (it automatically scans COM ports to hook onto Cardputer):
   ```bash
   python scripts/get_screenshot.py
   ```
3. **Capture**:
   Keep the script running. When you press the side button on Cardputer, the PC terminal will intercept the Base64 stream and reconstruct it into a lossless 24-bit `.bmp` file, stored under the `screenshot/` folder.

## User Interface Screenshots

| 3D Trajectory Orbit View | Satellite List & HAM Freqs | Shortcuts Help HUD |
|:---:|:---:|:---:|
| ![3D Orbits](screenshot/skycompass_20260621_142730.png) | ![Encyclopedia](screenshot/skycompass_20260621_142358.png) | ![Help Dialog](screenshot/skycompass_20260621_142315.png) |

## Data Sources & Acknowledgments

- **TLE Orbital Data**: Special thanks to [CelesTrak](https://celestrak.org/) for providing high-precision, real-time Two-Line Element (TLE) datasets for satellite tracking and propagation.
- **Model Calibration & Reference**: Special thanks to [Tianwentong](https://tianwentong.com/) and [Heavens-Above](https://www.heavens-above.com/) for their simulated prediction results which served as essential references during model calibration and verification.


## License

Licensed under the [GNU General Public License v3.0 (GPLv3)](LICENSE).

