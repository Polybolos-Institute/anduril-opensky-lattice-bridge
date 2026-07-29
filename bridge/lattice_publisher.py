"""Lattice REST entity publisher (sandbox OAuth + Sandboxes Bearer)."""

from __future__ import annotations

import os
import time
from dataclasses import dataclass
from typing import Any, Optional

import requests

from .opensky_client import AircraftState


@dataclass
class PublishResult:
    status_code: int
    text: str = ""
    ok: bool = False
    entity_id: str = ""


class LatticePublisher:
    """Thin REST door - no C2 logic."""

    def __init__(
        self,
        endpoint: Optional[str] = None,
        client_id: Optional[str] = None,
        client_secret: Optional[str] = None,
        env_token: Optional[str] = None,
        timeout_s: float = 30.0,
    ) -> None:
        host = (endpoint or os.environ.get("LATTICE_ENDPOINT", "")).strip()
        scheme = "https"
        if host.startswith("http://"):
            scheme = "http"
            host = host[len("http://") :]
        elif host.startswith("https://"):
            host = host[len("https://") :]
        self._host = host.rstrip("/")
        # Loopback → HTTP so CI can use mock-lattice without TLS.
        bare = self._host.split(":")[0].lower()
        if scheme == "https" and bare in ("127.0.0.1", "localhost", "::1"):
            scheme = "http"
        self._base = f"{scheme}://{self._host}" if self._host else ""
        self._client_id = (client_id or os.environ.get("LATTICE_CLIENT_ID", "")).strip()
        self._client_secret = (
            client_secret or os.environ.get("LATTICE_CLIENT_SECRET", "")
        ).strip()
        self._env_token = (env_token or os.environ.get("LATTICE_ENV_TOKEN", "")).strip()
        self._timeout = timeout_s
        self._access_token: Optional[str] = None
        self._token_expires_at = 0.0

    def missing_config(self) -> list[str]:
        missing = []
        if not self._host:
            missing.append("LATTICE_ENDPOINT")
        if not self._client_id:
            missing.append("LATTICE_CLIENT_ID")
        if not self._client_secret:
            missing.append("LATTICE_CLIENT_SECRET")
        if not self._env_token:
            missing.append("LATTICE_ENV_TOKEN")
        return missing

    def _sandbox_headers(self) -> dict[str, str]:
        return {
            "Anduril-Sandbox-Authorization": f"Bearer {self._env_token}",
            "Content-Type": "application/json",
            "Accept": "application/json",
        }

    def fetch_token(self) -> str:
        url = f"{self._base}/api/v1/oauth/token"
        data = {
            "grant_type": "client_credentials",
            "client_id": self._client_id,
            "client_secret": self._client_secret,
        }
        headers = {
            "Anduril-Sandbox-Authorization": f"Bearer {self._env_token}",
            "Content-Type": "application/x-www-form-urlencoded",
            "Accept": "application/json",
        }
        r = requests.post(url, headers=headers, data=data, timeout=self._timeout)
        r.raise_for_status()
        body = r.json()
        token = body.get("access_token")
        if not token:
            raise RuntimeError("oauth response missing access_token")
        expires_in = float(body.get("expires_in", 1800))
        self._access_token = token
        self._token_expires_at = time.time() + max(60.0, expires_in - 60.0)
        return token

    def ensure_token(self) -> str:
        if self._access_token and time.time() < self._token_expires_at:
            return self._access_token
        return self.fetch_token()

    def auth_only(self) -> dict[str, Any]:
        token = self.fetch_token()
        return {
            "ok": True,
            "endpoint": self._host,
            "token_prefix": token[:12] + "...",
            "expires_at": self._token_expires_at,
        }

    def _auth_headers(self) -> dict[str, str]:
        h = self._sandbox_headers()
        h["Authorization"] = f"Bearer {self.ensure_token()}"
        return h

    def state_to_entity(self, ac: AircraftState) -> dict[str, Any]:
        now = time.time()
        created = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now))
        expiry = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now + 120))
        name = (ac.callsign or ac.icao24).strip() or ac.icao24
        entity_id = f"polybolos-adsb-{ac.icao24.lower()}"
        return {
            "entityId": entity_id,
            "description": f"OpenSky ADS-B {name} ({ac.origin_country or 'UNK'})",
            "isLive": True,
            "createdTime": created,
            "expiryTime": expiry,
            "aliases": {"name": name},
            "milView": {
                "disposition": "DISPOSITION_UNKNOWN",
                "environment": "ENVIRONMENT_AIR",
            },
            "location": {
                "position": {
                    "latitudeDegrees": ac.lat_deg,
                    "longitudeDegrees": ac.lon_deg,
                    "altitudeHaeMeters": ac.alt_m if ac.alt_m is not None else 0.0,
                }
            },
            "ontology": {
                "template": "TEMPLATE_TRACK",
                "platformType": "ADS-B AIRPLANE",
            },
            "provenance": {
                "dataType": "adsb",
                "integrationName": "polybolos-opensky-lattice-bridge",
                "sourceUpdateTime": created,
            },
            "dataClassification": {
                "default": {"level": "CLASSIFICATION_LEVELS_UNCLASSIFIED"}
            },
        }

    def publish_state(self, ac: AircraftState) -> PublishResult:
        if not ac.valid:
            raise ValueError("aircraft state is not valid")
        entity = self.state_to_entity(ac)
        entity_id = entity["entityId"]
        # Proven HOTL path: PUT /api/v1/entities with entityId in body
        url = f"{self._base}/api/v1/entities"
        r = requests.put(
            url, headers=self._auth_headers(), json=entity, timeout=self._timeout
        )
        return PublishResult(
            status_code=r.status_code,
            text=r.text,
            ok=(200 <= r.status_code < 300),
            entity_id=entity_id,
        )
