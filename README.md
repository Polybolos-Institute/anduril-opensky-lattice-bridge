# Anduril Lattice - OpenSky / ADS-B bridge (door only)

## Status & recognition (factual)

> Independent Polybolos Institute sample (not an Anduril product).  
> OASW(SO/LIC) Jul 2026 **Selected** (technically meritorious; under evaluation/consideration).  
> AFRL Apr 2026: RQ portfolio share (Col Rondeau) + Control Science Center exchange (Weintraub; “state of the art” / partnership / SBIR language in correspondence). Attributed dialogue.  
> TRL 5 Decision-C2 lineage · Lattice sandbox / interop sample · Inquiries: mark.brown@polybolos.org · CAGE 1AVY9 · UEI RUSHH9B2UQV3

Standalone **door** that polls OpenSky Network (ADS-B state vectors) and
publishes Anduril Lattice World Model entities. No C2 core, no ROE, no engagement
authority.

Built by [Polybolos Institute](https://www.polybolos.org) for Lattice sandbox
interoperability demos. Complements (does not replace) HOTL / sealed Core.
**Independent sample - not an Anduril product.**

| Direction | Behavior |
|-----------|----------|
| **Up** | OpenSky `states/all` bbox -> Lattice entity PUT |
| **Sim** | Synthetic ADS-B tracks (`--sim`) for sandbox smoke |
| **Auth** | OAuth client-credentials + Sandboxes Bearer (`--auth-only`) |

**Primary implementation: C++ (WinHTTP)** - same TLS/auth path as the MAVLink
door. A thin Python reference remains under `bridge/`.

## Why this exists

Anduril ships [AIS -> Lattice](https://github.com/anduril/sample-app-ais-integration-rest)
samples. Community has [MAVLink -> Lattice](https://github.com/ARK-Electronics/mavlink-to-lattice)
(Python/SDK) and our [anduril-mavlink-lattice-bridge](https://github.com/Polybolos-Institute/anduril-mavlink-lattice-bridge)
(C++/WinHTTP). **ADS-B / OpenSky -> Anduril Lattice** fills that gap.

## Not in scope

- POLYBOLOS Core / ThreatObject / ROE / magazine / HOTL Authority
- Dump1090/readsb UDP (easy follow-on; same entity mapper)
- Effector / tasking path

## Credentials

| Piece | Required? |
|-------|-----------|
| Lattice (`LATTICE_*`) | Yes - same Sandboxes env as other doors |
| OpenSky account | **No** - anonymous public API (rate-limited) |

## Prerequisites

- Windows + CMake 3.21+ + MSVC (WinHTTP)
- Lattice Sandbox credentials (Developer Program)

## Build (C++)

```powershell
cd cpp
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Binary: `cpp\build\Release\opensky_lattice_bridge.exe`

## Run (C++)

```powershell
# load LATTICE_* then:
.\cpp\build\Release\opensky_lattice_bridge.exe --auth-only
.\cpp\build\Release\opensky_lattice_bridge.exe --sim --once
.\cpp\build\Release\opensky_lattice_bridge.exe --once          # live OpenSky (DFW bbox)
.\cpp\build\Release\opensky_lattice_bridge.exe --poll 15
```

Optional bbox env: `OPENSKY_LAMIN` `OPENSKY_LOMIN` `OPENSKY_LAMAX` `OPENSKY_LOMAX`
(default Dallas / DFW area).

## Python reference (optional)

```bash
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
python -m bridge --sim --once
```

## Architecture

```
OpenSky REST --> OpenSkyClient --> AircraftState[]
                                      |
                                      v
                              LatticeRestClient --> PUT /api/v1/entities
```

## Related doors

- [anduril-mavlink-lattice-bridge](https://github.com/Polybolos-Institute/anduril-mavlink-lattice-bridge)
- [anduril-dump1090-lattice-bridge](https://github.com/Polybolos-Institute/anduril-dump1090-lattice-bridge)
- [anduril-lattice-rest-winhttp](https://github.com/Polybolos-Institute/anduril-lattice-rest-winhttp) - shared WinHTTP REST client
- [anduril-lattice-stream-watcher](https://github.com/Polybolos-Institute/anduril-lattice-stream-watcher) - read-only SSE watcher
- [anduril-mock-lattice](https://github.com/Polybolos-Institute/anduril-mock-lattice) - CI sandbox stand-in
- [anduril-lattice-sandbox-dx](https://github.com/Polybolos-Institute/anduril-lattice-sandbox-dx) - auth / ontology DX



## License
MIT - see [LICENSE](LICENSE).

Anduril®, Lattice®, and Lattice SDK® are trademarks of Anduril Industries.
OpenSky Network is a separate project; respect their [API terms](https://opensky-network.org/).
This project is an independent integration sample, not an Anduril product.

## Contact

This repository is the open foundation (MIT).

Polybolos Institute also maintains a proprietary catalog of additional capabilities that are not published here. Contact us to discuss production deployment and commercial licensing.

mark.brown@polybolos.org · https://www.polybolos.org
