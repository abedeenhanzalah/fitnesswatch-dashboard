# FitnessWatch

[Open the live dashboard](https://abedeenhanzalah.github.io/fitnesswatch-dashboard/)

| Folder | Contents |
| --- | --- |
| `firmware/app` | Zephyr firmware for the XIAO nRF52840 Sense, with both I²C pin layouts |
| `firmware/tests` | Host-side C tests for the display cycle, battery, AQI, activity, pulse and telemetry |
| `site` | GitHub Pages dashboard for Chrome on Android and Chrome or Edge on Windows |
| `local-dashboard` | Local Windows dashboard with its Python server and AQI endpoint |
| `tools` | USB sensor tests, waveform plotting and wrist calibration capture |

## Build the firmware

Use a Zephyr workspace and SDK with support for `xiao_ble/nrf52840/sense`. The latest builds were made with Zephyr 4.5.0-rc2 and Zephyr SDK 1.0.1. Run these commands from the Zephyr workspace, replacing `/path/to/fitnesswatch-dashboard` with the path to this repository:

```sh
# Default I²C wiring (SDA D4, SCL D5)
west build -b xiao_ble/nrf52840/sense -s /path/to/fitnesswatch-dashboard/firmware/app -d build-d4-d5

# Alternate I²C wiring (SDA D9, SCL D10)
west build -b xiao_ble/nrf52840/sense -s /path/to/fitnesswatch-dashboard/firmware/app -d build-d9-d10 -- -DEXTRA_DTC_OVERLAY_FILE=/path/to/fitnesswatch-dashboard/firmware/app/d9-d10.overlay
```

The UF2 is in the selected build directory at `zephyr/zephyr.uf2`. On the connected board, press Reset twice to enter its UF2 bootloader and copy the matching file to the bootloader drive. The firmware includes the IIT Delhi startup logo, five-second screen rotation, battery gauge, daily step goal, IIT Delhi AQI, and Bluetooth screen configuration.

For a host-side check, compile a file from `firmware/tests` with a C compiler using `-std=c11 -I firmware/app/src -lm`, then run the resulting executable. Run the dashboard adapter tests from this repository with `node --test`.

## Run the local dashboard

On Windows, run `local-dashboard/start-watch-live.cmd`. Alternatively, run `python local-dashboard/watch-live-server.py` and open `http://127.0.0.1:8766/watch-live.html` in Chrome or Edge. Only one browser can connect to the watch at a time.

The USB utilities in `tools` require Python and `pyserial`. Waveform plotting also requires `numpy`, `scipy` and `matplotlib`.

## Publish a copy of the dashboard

1. Fork or copy this repository, including `.github/workflows/pages.yml`. On GitHub Free, use a public repository.
2. In the repository, open **Settings → Pages** and select **GitHub Actions** as the source.
3. In **Actions → FitnessWatch Pages**, click **Run workflow** on the default branch. Open the HTTPS link shown on the finished run or under **Settings → Pages**.

[GitHub Pages setup guide](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site)

## Connect the watch

1. Open the site directly in Chrome on Android or Chrome or Edge on Windows.
2. Disconnect the watch from any other browser first. Select **Connect watch**, then choose **FitnessWatch**. Grant Android's Nearby Devices permission if prompted.
3. Use **Watch screens** to change the OLED rotation and **Sync AQI to watch** to send the IIT Delhi station reading. These controls require a firmware build with screen configuration and IIT Delhi AQI support.

The browser communicates directly with the watch. GitHub serves the app and a public station reading. The site never sends watch readings to GitHub. The workflow fetches the IITM station reading twice an hour, and the dashboard rejects stale observations. GitHub may delay scheduled workflows. You can run the refresh manually from **Actions**.

GitHub Pages serves the `site` folder. For a local preview, run `python -m http.server 8766 --bind 127.0.0.1 --directory site` and visit `http://127.0.0.1:8766/`. Run `python scripts/refresh-aqi.py` first if you want to test AQI locally. The Bluetooth adapter tests use `node --test`.
