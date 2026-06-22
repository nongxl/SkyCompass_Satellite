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


## Technical Details: Orbital Propagations & Visibility Predictions

To match the orbital predictions of professional astronomy software, a comprehensive astronomical coordinate transformation, orbital propagation, and optical visibility calculation system is implemented on the ESP32 chip. The key physical models and mathematical formulas are described below:

### 1. Time System and Sidereal Time Conversions
* **Julian Date (JD)**
  The high-precision UTC timestamp $t_{unix}$ acquired from GNSS/NTP is converted to the Julian Date:
  $$\text{JD} = \frac{t_{unix}}{86400.0} + 2440587.5$$
* **Greenwich Mean Sidereal Time (GMST)**
  Due to Earth's rotation, transformations between inertial and terrestrial coordinate systems require calculating the Greenwich Mean Sidereal Time. This is computed using IAU astronomical formulas:
  $$T_0 = \frac{\text{JD}_0 - 2451545.0}{36525.0}$$
  $$\theta_{GMST} = \text{gmst\_0h\_sec} + \Delta t \times 1.002737909350795$$
  Where $\text{gmst\_0h\_sec}$ is the sidereal time at $0^h$ UT, and $\Delta t$ is the elapsed seconds of the day. The result is normalized to the range $[0, 2\pi]$ in radians.

### 2. Coordinate Transformations (TEME -> ECEF -> ENU)
* **TEME Inertial to Terrestrial (ECEF)**
  The spacecraft state vectors $(X_{TEME}, Y_{TEME}, Z_{TEME})$ output by the SGP4 propagator reside in the TEME (True Equator Mean Equinox) inertial frame. A rotation using the GMST angle $\theta_{GMST}$ projects them to the ECEF (Earth-Centered, Earth-Fixed) terrestrial frame:
  $$\begin{pmatrix} X_{ECEF} \\ Y_{ECEF} \\ Z_{ECEF} \end{pmatrix} = \begin{pmatrix} \cos\theta_{GMST} & \sin\theta_{GMST} & 0 \\ -\sin\theta_{GMST} & \cos\theta_{GMST} & 0 \\ 0 & 0 & 1 \end{pmatrix} \begin{pmatrix} X_{TEME} \\ Y_{TEME} \\ Z_{TEME} \end{pmatrix}$$
* **WGS-84 Earth Ellipsoid Model**
  To model Earth's flattening shape accurately, the WGS-84 ellipsoid parameters are adopted (semi-major axis $a = 6378.137\text{ km}$, first eccentricity squared $e^2 = 0.00669437999014$).
  * **Geodetic to ECEF**:
    The observer's coordinates (latitude $\phi$, longitude $\lambda$, and altitude $h$, aligned to user altitude `baseUserAlt`) are converted to the 3D ECEF position $(X_{obs}, Y_{obs}, Z_{obs})$:
    $$N(\phi) = \frac{a}{\sqrt{1 - e^2 \sin^2\phi}}$$
    $$X_{obs} = (N(\phi) + h) \cos\phi \cos\lambda, \quad Y_{obs} = (N(\phi) + h) \cos\phi \sin\lambda, \quad Z_{obs} = (N(\phi)(1 - e^2) + h) \sin\phi$$
  * **ECEF to Geodetic (Ground Track)**:
    An iterative method with 5 iterations is implemented to calculate the satellite's latitude/longitude and altitude above the ellipsoid.
* **Topocentric Coordinate System (ENU: East-North-Up)**
  To solve the satellite's position relative to the local observer, the delta vector $\mathbf{d} = \mathbf{r}_{sat} - \mathbf{r}_{obs}$ is projected onto the local horizon plane, producing the East-North-Up (ENU) coordinates $(E, N, U)$. From these, the Azimuth ($Az$), Elevation ($El$), and Range ($Range$) are derived:
  $$Range = \sqrt{E^2 + N^2 + U^2}$$
  $$Az = \text{atan2}(E, N) \pmod{360^\circ}, \quad El = \arcsin\left(\frac{U}{Range}\right)$$

### 3. Atmospheric Refraction Correction
For satellites at low elevations, Earth's atmosphere bends incoming light, making the satellite appear slightly higher than its geometric position. An atmospheric refraction compensation is applied when the geometric elevation is between $-5^\circ < El < 15^\circ$:
$$R = \frac{1.02}{\tan\left(El + \frac{10.3}{El + 5.11}\right)} \quad (\text{arcminutes})$$
$$El_{corrected} = El + \frac{R}{60^\circ}$$
This correction aligns the predicted Acquisition of Signal (AOS) and Loss of Signal (LOS) times with professional online databases such as Heavens-Above.

### 4. Tri-Criterion Optical Visibility
For a spacecraft to be visible to the naked eye, three conditions must be satisfied simultaneously:
1. **Minimum Elevation**: The corrected elevation must satisfy $El_{corrected} \ge 10.0^\circ$ to clear local obstructions and atmospheric extinction.
2. **Local Dark Sky**: The Sun's local elevation at the observer's location $\theta_{sun}$ must be $\theta_{sun} < -6.0^\circ$ (civil twilight has ended, skies are dark).
3. **Earth Umbra Check (Illuminated Satellite)**:
   The satellite must be illuminated by sunlight. A spherical approximation is used to determine if the satellite is in Earth's shadow.
   The cosine of the angle between the subsolar point and the satellite is:
   $$\cos\theta = \sin(subLat_R)\sin(lat_R) + \cos(subLat_R)\cos(lat_R)\cos(lon_R - subLon_R)$$
   If $\cos\theta < 0$, the perpendicular distance to the shadow axis is computed:
   $$d_{shadow} = d_{sat} \times \sqrt{1 - \cos^2\theta}$$
   If $d_{shadow} < 6378.137\text{ km}$, the satellite is eclipsed by Earth's shadow (in Earth's umbra) and receives no sunlight, making it optically invisible.

### 5. Visual Magnitude, Phase Angle & Atmospheric Extinction
To estimate the satellite's brightness, the visual magnitude ($M$) is calculated by taking into account the standard magnitude ($M_{std}$, defined at $1000\text{ km}$ distance and full phase), observer distance ($Range$), solar phase angle, and atmospheric extinction:
* **Phase Angle ($\psi$)**: The angle "Sun - Satellite - Observer".
* **Diffuse Sphere Phase Function ($\Phi(\psi)$)**: Models diffuse solar reflection scattering from a spherical surface:
  $$\Phi(\psi) = \sin(\psi) + (\pi - \psi)\cos(\psi)$$
* **Atmospheric Extinction ($\Delta M_{ext}$)**:
  To match specialized platforms like Tianwentong in low-elevation extinction fading, the Kasten-Young Air Mass ($AM$) formulation is integrated with a visual band extinction coefficient $k_v = 0.18$:
  $$AM \approx \frac{1}{\sin(El_{corrected}) + 0.15(El_{corrected} + 3.825)^{-1.253}}$$
  $$\Delta M_{ext} = 0.18 \times AM$$
* **Apparent Magnitude Formulation**:
  $$M = M_{std} + 5.0 \log_{10}\left(\frac{Range}{1000\text{ km}}\right) - 2.5 \log_{10}\left(\Phi(\psi)\right) + \Delta M_{ext}$$
  This refined model accounts for both solar geometry and low-elevation atmospheric absorption/scattering, ensuring high-fidelity correlation with mainstream astronomical tools.

### 6. Power-Efficient Dual-Step Prediction Engine
To bypass ESP32's processing limits during 24-hour pass searches, a dual-step propagation technique is employed:
* The propagator sweeps forward using a coarse **120-second (2-minute)** step size to quickly bypass long periods of invisibility.
* Once the elevation rises above the horizon, the engine **rewinds the timeline by 120 seconds** and switches to a fine **10-second** step size for precise calculations (determining the exact AOS, peak elevation time, magnitude, and LOS).
* Once the satellite sets, the engine resumes the coarse 120-second steps, optimizing precision and CPU cycle consumption.

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
- **Model Calibration & Reference**: Special thanks to [Tianwentong](https://laysky.com/) and [Heavens-Above](https://www.heavens-above.com/) for their simulated prediction results which served as essential references during model calibration and verification.



## License

Licensed under the [GNU General Public License v3.0 (GPLv3)](LICENSE).

