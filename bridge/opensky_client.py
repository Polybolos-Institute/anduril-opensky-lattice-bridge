"""OpenSky Network ADS-B state client (public REST)."""

from __future__ import annotations

import math
import os
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

import requests


@dataclass
class AircraftState:
    icao24: str
    callsign: str = ""
    origin_country: str = ""
    lat_deg: float = 0.0
    lon_deg: float = 0.0
    alt_m: Optional[float] = None
    heading_deg: float = 0.0
    speed_mps: float = 0.0
    on_ground: bool = False
    valid: bool = False
    source: str = "opensky"


class OpenSkyClient:
    """Poll OpenSky /api/states/all for a geographic bbox."""

    def __init__(
        self,
        lamin: Optional[float] = None,
        lomin: Optional[float] = None,
        lamax: Optional[float] = None,
        lomax: Optional[float] = None,
        user: Optional[str] = None,
        password: Optional[str] = None,
        timeout_s: float = 30.0,
    ) -> None:
        # Default: Dallas / DFW area
        self.lamin = float(os.environ.get("OPENSKY_LAMIN", lamin if lamin is not None else 32.5))
        self.lomin = float(os.environ.get("OPENSKY_LOMIN", lomin if lomin is not None else -97.3))
        self.lamax = float(os.environ.get("OPENSKY_LAMAX", lamax if lamax is not None else 33.1))
        self.lomax = float(os.environ.get("OPENSKY_LOMAX", lomax if lomax is not None else -96.4))
        self.user = (user or os.environ.get("OPENSKY_USER", "")).strip() or None
        self.password = (password or os.environ.get("OPENSKY_PASSWORD", "")).strip() or None
        self.timeout_s = timeout_s

    def fetch_states(self) -> Tuple[List[AircraftState], Optional[str]]:
        """Returns (states, error_message)."""
        url = "https://opensky-network.org/api/states/all"
        params = {
            "lamin": self.lamin,
            "lomin": self.lomin,
            "lamax": self.lamax,
            "lomax": self.lomax,
        }
        auth = (self.user, self.password) if self.user and self.password else None
        try:
            r = requests.get(url, params=params, auth=auth, timeout=self.timeout_s)
        except requests.RequestException as e:
            return [], f"opensky request failed: {e}"
        if r.status_code == 429:
            return [], "opensky rate limited (429) - slow poll or add OPENSKY_USER/PASSWORD"
        if r.status_code != 200:
            return [], f"opensky HTTP {r.status_code}: {r.text[:200]}"
        body = r.json()
        raw = body.get("states") or []
        out: List[AircraftState] = []
        for row in raw:
            ac = _parse_state_row(row)
            if ac and ac.valid and not ac.on_ground:
                out.append(ac)
        return out, None


def _parse_state_row(row: list) -> Optional[AircraftState]:
    # OpenSky state vector indices:
    # 0 icao24, 1 callsign, 2 origin_country, 5 lon, 6 lat, 7 baro_alt,
    # 8 on_ground, 9 velocity, 10 true_track, ...
    if not isinstance(row, list) or len(row) < 11:
        return None
    icao = (row[0] or "").strip()
    if not icao:
        return None
    lon = row[5]
    lat = row[6]
    if lon is None or lat is None:
        return None
    alt = row[7]
    vel = row[9]
    track = row[10]
    return AircraftState(
        icao24=icao,
        callsign=(row[1] or "").strip(),
        origin_country=(row[2] or "").strip(),
        lat_deg=float(lat),
        lon_deg=float(lon),
        alt_m=float(alt) if alt is not None else None,
        heading_deg=float(track) if track is not None else 0.0,
        speed_mps=float(vel) if vel is not None else 0.0,
        on_ground=bool(row[8]),
        valid=True,
        source="opensky",
    )


def sim_states(n: int = 3) -> List[AircraftState]:
    """Synthetic ADS-B tracks near Dallas for sandbox smoke."""
    t = time.time()
    lat0, lon0 = 32.8990, -97.0403  # DFW
    out: List[AircraftState] = []
    for i in range(n):
        ang = (t / 40.0 + i * (2.0 * math.pi / max(n, 1))) % (2.0 * math.pi)
        dlat = (0.08 * math.cos(ang + i)) 
        dlon = (0.10 * math.sin(ang + i))
        icao = f"sim{i:03d}"
        out.append(
            AircraftState(
                icao24=icao,
                callsign=f"SIM{i+1:03d}",
                origin_country="United States",
                lat_deg=lat0 + dlat,
                lon_deg=lon0 + dlon,
                alt_m=3000.0 + 250.0 * i,
                heading_deg=(math.degrees(ang) + 90.0) % 360.0,
                speed_mps=120.0,
                on_ground=False,
                valid=True,
                source="sim",
            )
        )
    return out
