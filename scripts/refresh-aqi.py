"""Write the public IIT Delhi station reading for the static dashboard."""

import json
import re
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
from urllib.request import urlopen


AQI_SOURCE = "https://ews.tropmet.res.in/"
AQI_STATION = "IIT Delhi, Delhi - IITM"
IST = timezone(timedelta(hours=5, minutes=30))
MAX_PAGE_BYTES = 8 * 1024 * 1024


def read_station_aqi(html):
    feed = re.search(r"<script>\s*window\.EWS_CPCB_FEED\s*=\s*", html)
    if not feed:
        raise ValueError("IITM station feed was not found")
    stations, _ = json.JSONDecoder().raw_decode(html, feed.end())
    for station in stations:
        if station.get("name", "").strip() != AQI_STATION:
            continue
        aqi = station.get("aqi")
        if type(aqi) is not int or not 0 <= aqi <= 500:
            raise ValueError("IIT Delhi AQI is unavailable")
        observation = datetime.strptime(station["time"], "%d-%m-%Y %H:%M:%S").replace(tzinfo=IST)
        return {"aqi": aqi, "observedAt": int(observation.timestamp())}
    raise ValueError("IIT Delhi station was not found")


def main():
    try:
        with urlopen(AQI_SOURCE, timeout=12) as response:
            page = response.read(MAX_PAGE_BYTES + 1)
        if len(page) > MAX_PAGE_BYTES:
            raise ValueError("IITM page exceeds the size limit")
        reading = read_station_aqi(page.decode("utf-8"))
    except (OSError, ValueError, TypeError, AttributeError, KeyError) as error:
        sys.exit(f"IIT Delhi AQI fetch failed: {error}")

    target = Path(__file__).resolve().parents[1] / "site" / "aqi.json"
    target.write_text(json.dumps(reading) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
