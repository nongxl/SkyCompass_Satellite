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
  - `R`: Reset the **Time Machine** simulated offset, immediately reverting to actual system time.
  - `Del` (Backspace): Toggle **HUD Data Overlay** (toggles latitude/longitude overlays at corners).
  - `[` and `]`: Adjust TFT screen brightness (from 16 to 255).
- **Live Mode & Time Machine (Simulated Time Travel)**:
  - Vector 3D Earth rendering with **dynamic cold/warm gradient continent outlines** and daylight/shadow terminator.
  - **Visual Anchors**: Automatically computes and floats a "dynamic compass" at the screen corner; projects a "3D axis & polar crosshair grid" at polar zones to resolve disorientation when looking from an orbital perspective.
  - Press **, (backward)** or **/ (forward)** to toggle the **Time Machine**, supporting long-press for high-speed time travel to preview future passes. When time is offset, the bottom-right HUD clock will turn **yellow** to provide a clear visual cue; press **`R`** on the main screen to immediately reset the simulated time back to actual system time (the clock font color will return to white).
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
- **Mission-Driven Architecture**:
  To prevent Out-Of-Memory (OOM) crashes caused by eagerly loading and calculating large spacecraft groups (e.g. Starlink launches containing 20-30 satellites), the Recent Launch module is refactored around a **Mission First** architecture:
  - **3-Level Navigation**: Structured as Mission List (Level 1) -> Mission Overview (Level 2 with Keplerian orbital altitude/inclination, launch age, stars rating and estimated visibility) -> Objects View (Level 3 which lazy-loads exactly 5 satellites in memory, destroying and freeing all SGP4 allocations on exit or page flip).
  - **Mission Visualization & Dynamic Starlink Train**: Decouples physical propagation from visual rendering. Based on the **Launch Age**, it dynamically categorizes the mission into four states: `VERY_TIGHT` (Tight Train), `TIGHT` (Dense Train), `EXPANDING` (Dispersing), and `OPERATIONAL` (Uniformly distributed). It introduces a **Phase Offset** model to simulate the cumulative orbital spacing over time. Using adaptive marker densities, subtle breathing brightness animations, and a smooth time-driven slide motion, it realistically renders constellation deployment on the 3D globe with zero extra SGP4 and RAM overhead!
- **Tailored Satellite Type UI (Customized Radio & Observation Layouts)**:
  Satellites are divided into 5 distinct categories according to their physical properties and radio payloads. Customized UI layouts are dynamically rendered in the Encyclopedia Panel, Tracking HUD, and Sidebar List HUD, preventing guess-work or fabrication of radio frequencies:
  - **HAM (Amateur Radio Satellites)**: Displays real-time Doppler-compensated Rx frequency, Tx uplink frequency with CTCSS sub-tones (Tone), and modulation mode. When not tracking, it dynamically calculates and displays the upcoming AOS, LOS, and Peak Elevation (Max El).
  - **WEATHER (Meteorological Satellites)**: Such as NOAA or Meteor series. Renders only the Doppler-compensated Rx frequency and weather imaging mode (e.g. APT/LRPT), hiding unused Tx uplink parameters.
  - **SPACE_STATION (Space Stations)**: For the complex multi-radio payloads of the ISS, it custom-renders dual-channel Doppler shift frequencies for APRS (145.825 MHz) and SSTV (145.800 MHz) simultaneously. For space stations without active HAM operations (e.g. Tiangong), it displays no amateur radio capability.
  - **VISUAL (Visual-only Spacecrafts)**: Such as the Hubble Space Telescope, JWST, and rocket bodies. Displays `No Amateur Radio Capability` explicitly, without generating fake radio data.
  - **HISTORICAL (Inactive Monuments)**: Such as China's first satellite Dong Fang Hong I (DFH-1). Shows the launch date (1970) and inactive status. It hides the radio frequency panel completely in both tracking and non-tracking screens.


## Technical Details: Orbital Propagations & Visibility Predictions

To match the orbital predictions of professional astronomy software, a comprehensive astronomical coordinate transformation, orbital propagation, and optical visibility calculation system is implemented on the ESP32 chip. The key physical models and technical summaries are described below:

### 1. Time System and Sidereal Time Conversions
* **Julian Date (JD)**
  The system converts the high-precision UTC timestamp acquired from GNSS/NTP to the Julian Date (JD), providing a precise time baseline for subsequent SGP4 model computations.
* **Greenwich Mean Sidereal Time (GMST)**
  Due to Earth's rotation, transformations between inertial and terrestrial coordinate systems require calculating the Greenwich Mean Sidereal Time (GMST). This is computed using standard astronomical formulas to determine the Earth's current rotation angle, which is normalized to the range [0, 2π] in radians.

### 2. Coordinate Transformations (TEME -> ECEF -> ENU)
* **TEME Inertial to Terrestrial (ECEF)**
  The spacecraft state vectors output by the SGP4 propagator reside in the TEME (True Equator Mean Equinox) inertial frame. A rotation matrix using the GMST angle projects them to the ECEF (Earth-Centered, Earth-Fixed) terrestrial frame, transforming the inertial position coordinates into Earth-surface 3D coordinates.
* **WGS-84 Earth Ellipsoid Model**
  To model Earth's flattening shape accurately, the WGS-84 ellipsoid parameters are adopted (introducing semi-major axis and flattening parameters).
  - **Geodetic to ECEF**: The observer's coordinates (latitude, longitude, and altitude, automatically acquired from GNSS or network positioning) are converted to the 3D ECEF position.
  - **ECEF to Geodetic (Ground Track)**: An iterative method is implemented to solve and correct geodetic distortion, yielding the exact ground track (latitude/longitude) and altitude of the satellite.
* **Topocentric Coordinate System (ENU: East-North-Up)**
  To solve the satellite's position relative to the local observer, the delta vector is projected onto the local horizon plane, producing the East-North-Up (ENU) coordinates. From these, the Azimuth (Az), Elevation (El), and Range (Range) are derived.

### 3. Atmospheric Refraction Correction
For satellites at low elevations, Earth's atmosphere bends incoming light, making the satellite appear slightly higher than its geometric position. An atmospheric refraction compensation is applied when the elevation is low, aligning the predicted Acquisition of Signal (AOS) and Loss of Signal (LOS) times with professional online databases such as Heavens-Above.

### 4. Tri-Criterion Optical Visibility
For a spacecraft to be visible to the naked eye, three conditions must be satisfied simultaneously:
1. **Minimum Elevation**: The corrected elevation must satisfy a minimum threshold (typically 10.0°) to clear local obstructions and atmospheric refraction.
2. **Local Dark Sky**: The Sun's local elevation at the observer's location must be below civil twilight (typically less than -6.0°), indicating the sky is dark.
3. **Earth Umbra Check (Illuminated Satellite)**: The satellite must be illuminated by sunlight. A spherical approximation is used to model Earth's shadow cone and determine if the satellite is eclipsed (in Earth's umbra) and receives no sunlight, making it optically invisible.

### 5. Visual Magnitude, Phase Angle & Atmospheric Extinction
To estimate the satellite's brightness, the visual magnitude is calculated by taking into account the standard magnitude, observer distance, solar phase angle, and atmospheric extinction:
* **Phase Angle (ψ)**: Calculates the "Sun - Satellite - Observer" three-dimensional angle to determine the reflection geometry.
* **Diffuse Sphere Phase Function**: Models diffuse solar reflection scattering from a spherical surface to represent how sunlight scatters off the satellite's body.
* **Atmospheric Extinction**: Integrates the Kasten-Young Air Mass model with an atmospheric extinction coefficient to correct for brightness attenuation at low elevation angles due to the thicker air mass.
* **Apparent Magnitude Formulation**: Synthesizes range, phase angle, and low-elevation atmospheric extinction to compute the apparent magnitude, ensuring high-fidelity correlation with mainstream astronomical tools.

### 6. Power-Efficient Dual-Step Prediction Engine & Time Machine Optimization
To bypass ESP32's processing limits during 24-hour pass searches, several energy-efficiency algorithms are implemented:
* **Dual-Step Propagation**: The propagator sweeps forward using a coarse **120-second (2-minute)** step size to quickly bypass long periods of invisibility. Once the elevation rises above the horizon, the engine **rewinds the timeline by 120 seconds** and switches to a fine **10-second** step size for precise calculations (determining the exact AOS, peak elevation time, magnitude, and LOS). Once the satellite sets, the engine resumes the coarse 120-second steps, optimizing precision and CPU cycle consumption.
* **Scrubbing Cooldown Gate**: Since shortening the prediction step to 10 seconds increases the pass calculator load, rapid time axis scrubbing (such as holding keys `,`/`/` or fast-tapping in Time Machine) could trigger high-frequency recalculations of full 3D orbit paths for all selected satellites (each requiring SGP4 coordinates propagation and geodetic coordinate iterations for 30 steps). To maintain smooth inputs, a **120ms physical cooldown gate** is implemented in `calculateOrbit`. During active scrubbing, any heavy path recalculation is blocked within a 120ms window, rendering only the core spacecraft real-time position. The orbital paths instantly redraw once keys are released or scrolling stops, completely eliminating lag spikes and maintaining a locked **60 FPS** frame rate.

### 7. Amateur Radio (HAM) Parameters & Doppler Shift Calculations
For satellites supporting Amateur Radio (HAM) payloads (such as FM transponder satellites SO-50, AO-91, or the International Space Station ISS which carries multiple radio setups), the system displays real-time radio tuning parameters in the Encyclopedia and Tracking HUDs. The physical meanings and mathematical definitions are as follows:

* **RX (Receive - Downlink Frequency)**: The frequency on which the satellite transmits and the ground station receives. The RX value displayed in the UI is the **real-time Doppler-compensated frequency**, indicating the actual frequency to tune your receiver to.
* **TX (Transmit - Uplink Frequency)**: The frequency on which the ground station transmits to the satellite. The UI displays the static center frequency (in practice, the operator needs to manually apply reverse Doppler compensation on their transmitter).
* **T (Tone - CTCSS Sub-tone)**: Continuous Tone-Coded Squelch System. Displayed in the UI as `T: [Hz]` (e.g., `T:67.0`). This sub-audible tone is required to open the squelch of the satellite's FM repeater. If your transmitter does not inject this specific sub-tone, the satellite will not relay your voice. A value of `None` indicates no sub-tone is required.
* **Doppler Deviation (The bracketed number)**: Shown to the right of the RX frequency (e.g., `(+1.5k)` or `(-3.2k)`), this indicates the **real-time Doppler frequency shift $\Delta f$ in kHz**.
  * **Physical Model of Doppler Shift**:
    Because the satellite moves at high orbital velocity (approx. 7.8 km/s) relative to the observer, the received radio frequency shifts. The Doppler shift is computed dynamically based on the relationship between the nominal center frequency, radial velocity, and the speed of light. Frequency shifts higher as the satellite approaches and lower as it recedes.
* **Operational Meaning for Operators**:
    * **Acquisition of Signal (AOS)**: The satellite approaches rapidly. The Doppler shift reaches its positive maximum (e.g., `+3.5k`). Operators must tune their receivers higher.
    * **Time of Closest Approach (TCA / Max El)**: The radial velocity is zero, yielding $\Delta f = 0$. The received frequency matches the nominal center frequency.
    * **Loss of Signal (LOS)**: The satellite recedes rapidly. The Doppler shift reaches its negative maximum (e.g., `-3.5k`). Operators must tune their receivers lower.
* **Difference Between RX1/RX2 and U/D**:
  * In standard amateur satellite operations, **U (Uplink)** refers to the transmission frequency, and **D (Downlink)** refers to the reception frequency.
  * For complex platforms like the ISS, multiple downlink channels can be active simultaneously (e.g., APRS packet radio at 145.825 MHz, and SSTV image downlink at 145.800 MHz). To optimize screen real estate and deliver maximum situational awareness, the system designates these dual channels as **RX1** and **RX2** side-by-side. This allows operators to track Doppler shifts for both downlinks simultaneously, rather than showing a generic single uplink/downlink (U/D) layout.

## Helper Utilities & Build Scripts

To minimize runtime overhead on the ESP32 and facilitate data preparation, several offline pre-processing and PC-side utility scripts are included in the repository:

### 1. Scripts Directory (`scripts/`)
1. **[gen_light_points.py](scripts/gen_light_points.py)** (Nightlights Point Cloud Generator)
   * **Purpose**: Downloads global nightlight tiles from NASA's GIBS VIIRS Black Marble service, merges them into a 1024x1024 master map, and performs Stratified Importance Sampling to extract the 3000 brightest light pollution coords.
   * **Output**: Generates [light_points_data.h](src/core/light_points_data.h) containing pre-calculated spherical coordinates for fast, math-free 3D rendering of city glow on the globe.
2. **[generate_timezone_grid.py](scripts/generate_timezone_grid.py)** (Offline Timezone Grid Generator)
   * **Purpose**: Queries the timezone name and UTC offset for a $1^\circ \times 1^\circ$ global grid (180 rows $\times$ 360 columns), using nautical timezones to fill ocean zones.
   * **Output**: Generates [timezone_grid.h](src/core/timezone_grid.h), enabling the device to determine the correct local UTC offset offline instantly when GPS lock is acquired.
3. **[get_screenshot.py](scripts/get_screenshot.py)** (PC Serial Screenshot Receiver)
   * **Purpose**: Monitors the Cardputer serial port. Intercepts the Base64-encoded frame buffer segments (using the magic prefix `==SKYCOMPASS_DATA==`) and filters terminal escape codes.
   * **Output**: Decodes the raw big-endian RGB565 buffer into 24-bit lossless BMP images saved under `screenshot/`, bypassing the ESP32's PSRAM restrictions.
4. **[optimize_earth_data.py](scripts/optimize_earth_data.py)** (Continental Lines Trigonometric Pre-processor)
   * **Purpose**: Performs offline pre-computation of trigonometric values ($\sin Lat$, $\cos Lat$, $\sin Lon$, $\cos Lon$) for the continental polygon coordinates.
   * **Output**: Refactors [earth_data.h](src/core/earth_data.h). This eliminates expensive trigonometric calls during 3D rotations, ensuring smooth 30 FPS globe rendering.
5. **[update_frequencies.py](scripts/update_frequencies.py)** (HAM Downlink Frequency Fetcher)
   * **Purpose**: Runs inside the GitHub Action workflow to fetch satellite transceiver frequencies from public databases (e.g., SatNOGS DB) and filter them down to active amateur satellites.
   * **Output**: Updates `data/frequencies.json` and pushes to GitHub Pages for over-the-air (OTA) updates.

### 2. Scratch Directory (`scratch/`)
The `scratch/` folder hosts development tools, math verifications, and refactoring utilities used during the construction of the application:
* **Orbit & Coordinate Verification**:
  * [test_sgp4_2024.py](scratch/test_sgp4_2024.py) / [test_sgp4_2026.py](scratch/test_sgp4_2026.py): Feeds TLEs into the Python `sgp4` library to output reference state vectors, allowing cross-verification of the ESP32 C++ SGP4 library numerical output.
  * [test_gmst.py](scratch/test_gmst.py) / [verify_coord.py](scratch/verify_coord.py): Compares Greenwich Mean Sidereal Time calculations and ECEF-to-ENU coordinate mappings against astronomical standard references.
* **Lighting & Shadow Simulations**:
  * [test_shadow4.py](scratch/test_shadow4.py) / [test_shadow5.py](scratch/test_shadow5.py): Prototypes of the Earth cylindrical/spherical umbra shadow math to verify exit/entry thresholds prior to C++ translation.
  * [test_sun_alt.py](scratch/test_sun_alt.py) / [test_sun_alt_exact.py](scratch/test_sun_alt_exact.py): Solves the subsolar point and solar elevations to calibrate the day/night terminator lines.
* **Stars and Sky Map Generation**:
  * [generate_stars.py](scratch/generate_stars.py): Parses the Yale Bright Star Catalog to filter and export high-brightness navigation stars (e.g., Sirius) into C headers.
  * [test_star_proj.py](scratch/test_star_proj.py): Validates orthographic camera projections for stars on the background canvas.
* **Automated Refactoring & Patching**:
  * [update_main.py](scratch/update_main.py) / [patch_main.py](scratch/patch_main.py) / [modify.py](scratch/modify.py): Automates text-search-replace operations and safety-checking insertions into the large `src/main.cpp` code file during remote pair programming sessions.
  * [benchmark.cpp](scratch/benchmark.cpp) / [test5.cpp](scratch/test5.cpp): Measures computation latency and runs compiler math optimizations benchmarks on the physical target hardware.

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
- On any screen, press the physical **BtnA button (GPIO0, located on the right side of the switch)**.
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
- **Model Calibration & Reference**: Special thanks to [Tianwentong](https://laysky.com/) and [Heavens-Above](https://www.heavens-above.com/) for their simulated prediction results which served as essential references during model calibration and verification.



## License

Licensed under the [GNU General Public License v3.0 (GPLv3)](LICENSE).

