"""CLI: OpenSky / ADS-B → Lattice entity door."""

from __future__ import annotations

import argparse
import os
import sys
import time

from dotenv import load_dotenv

from .lattice_publisher import LatticePublisher
from .opensky_client import OpenSkyClient, sim_states


def _load_env() -> None:
    load_dotenv()
    # Optional: pull from sibling Polybolos lattice.local.ps1 via already-set env
    # (operator runs: . ..\lattice.local.ps1 before python).


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description="OpenSky ADS-B → Lattice entity door (no C2)"
    )
    p.add_argument("--auth-only", action="store_true", help="OAuth smoke only")
    p.add_argument("--sim", action="store_true", help="Synthetic tracks (no OpenSky)")
    p.add_argument("--once", action="store_true", help="Single poll then exit")
    p.add_argument(
        "--poll",
        type=float,
        default=None,
        metavar="SEC",
        help="Poll interval seconds (default 15 when looping)",
    )
    p.add_argument(
        "--max-tracks",
        type=int,
        default=int(os.environ.get("MAX_TRACKS", "25")),
        help="Max aircraft to publish per cycle (default 25)",
    )
    args = p.parse_args(argv)
    _load_env()

    pub = LatticePublisher()
    missing = pub.missing_config()
    if missing:
        print(f"[opensky-bridge] missing env: {' '.join(missing)}", file=sys.stderr)
        return 1

    if args.auth_only:
        info = pub.auth_only()
        print(f"[opensky-bridge] auth-only OK endpoint={info['endpoint']}")
        return 0

    poll_sec = args.poll if args.poll is not None else float(os.environ.get("POLL_SEC", "15"))
    client = OpenSkyClient()

    while True:
        if args.sim:
            states = sim_states(n=min(3, args.max_tracks))
            err = None
        else:
            states, err = client.fetch_states()

        if err:
            print(f"[opensky-bridge] {err}", file=sys.stderr)
            if args.once:
                return 1
            time.sleep(poll_sec)
            continue

        states = states[: max(0, args.max_tracks)]
        ok = 0
        fail = 0
        for ac in states:
            result = pub.publish_state(ac)
            if result.ok:
                ok += 1
                print(
                    f"[opensky-bridge] HTTP {result.status_code} ok id={result.entity_id} "
                    f"callsign={ac.callsign or '-'} lat={ac.lat_deg:.4f} lon={ac.lon_deg:.4f}"
                )
            else:
                fail += 1
                print(
                    f"[opensky-bridge] HTTP {result.status_code} FAIL id={result.entity_id} "
                    f"body={result.text[:160]}",
                    file=sys.stderr,
                )

        print(
            f"[opensky-bridge] cycle done source={'sim' if args.sim else 'opensky'} "
            f"n={len(states)} ok={ok} fail={fail}"
        )

        if args.once:
            return 0 if fail == 0 and ok > 0 else (0 if ok > 0 else 1)

        time.sleep(poll_sec)


if __name__ == "__main__":
    raise SystemExit(main())
