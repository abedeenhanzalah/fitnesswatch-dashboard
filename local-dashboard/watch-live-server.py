import json
import re
from datetime import datetime, timedelta, timezone
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
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


class DashboardHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(Path(__file__).resolve().parent), **kwargs)

    def do_GET(self):
        if self.path != "/api/iit-delhi-aqi":
            super().do_GET()
            return
        try:
            with urlopen(AQI_SOURCE, timeout=8) as response:
                page = response.read(MAX_PAGE_BYTES + 1)
            if len(page) > MAX_PAGE_BYTES:
                raise ValueError("IITM page exceeds the size limit")
            result = read_station_aqi(page.decode("utf-8"))
            status = 200
        except (OSError, ValueError, TypeError, AttributeError, KeyError) as error:
            self.log_error("IIT Delhi AQI fetch failed: %s", error)
            result = {"error": "IIT Delhi AQI is unavailable from IITM. Try again later."}
            status = 502
        body = json.dumps(result).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    with ThreadingHTTPServer(("127.0.0.1", 8766), DashboardHandler) as server:
        server.serve_forever()
