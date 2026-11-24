# FitnessWatch on GitHub Pages

## Publish

1. Extract the archive and add its contents to the root of a GitHub repository. Include `.github/workflows/pages.yml`. On GitHub Free, use a public repository.
2. In the repository, open **Settings → Pages** and select **GitHub Actions** as the source.
3. In **Actions → FitnessWatch Pages**, click **Run workflow** on the default branch. Open the HTTPS link shown on the finished run or under **Settings → Pages**.

[GitHub Pages setup guide](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site)

## Connect the watch

1. Open the site directly in Chrome on Android or Chrome or Edge on Windows.
2. Disconnect the watch from any other browser first. Select **Connect watch**, then choose **FitnessWatch**. Grant Android's Nearby Devices permission if prompted.
3. Use **Watch screens** to change the OLED rotation and **Sync AQI to watch** to send the IIT Delhi station reading. These controls require a firmware build with screen configuration and IIT Delhi AQI support.

The browser communicates directly with the watch. GitHub serves the app and a public station reading. The site never sends watch readings to GitHub. The workflow fetches the IITM station reading twice an hour, and the dashboard rejects stale observations. GitHub may delay scheduled workflows. You can run the refresh manually from **Actions**.

GitHub Pages serves the `site` folder. For a local preview, run `python -m http.server 8766 --bind 127.0.0.1 --directory site` and visit `http://127.0.0.1:8766/`. Run `python scripts/refresh-aqi.py` first if you want to test AQI locally. The Bluetooth adapter tests use `node --test`.
