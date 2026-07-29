#include "lattice_rest.hpp"
#include "opensky_client.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

const char* EnvOrEmpty(const char* key) {
  const char* v = std::getenv(key);
  return v ? v : "";
}

void PrintUsage() {
  std::cerr
      << "opensky_lattice_bridge - OpenSky ADS-B -> Lattice entity door "
         "(C++ / WinHTTP)\n"
      << "\n"
      << "Usage:\n"
      << "  opensky_lattice_bridge --auth-only\n"
      << "  opensky_lattice_bridge --sim [--once] [--max-tracks N]\n"
      << "  opensky_lattice_bridge [--once] [--poll SEC] [--max-tracks N]\n"
      << "\n"
      << "Lattice env (required):\n"
      << "  LATTICE_ENDPOINT LATTICE_CLIENT_ID LATTICE_CLIENT_SECRET "
         "LATTICE_ENV_TOKEN\n"
      << "\n"
      << "OpenSky: anonymous public API (no account). Optional bbox:\n"
      << "  OPENSKY_LAMIN OPENSKY_LOMIN OPENSKY_LAMAX OPENSKY_LOMAX\n";
}

olb::LatticeRestClient MakeClientFromEnv() {
  olb::LatticeRestClient client;
  client.SetEndpoint(EnvOrEmpty("LATTICE_ENDPOINT"));
  client.SetCredentials(EnvOrEmpty("LATTICE_CLIENT_ID"),
                        EnvOrEmpty("LATTICE_CLIENT_SECRET"),
                        EnvOrEmpty("LATTICE_ENV_TOKEN"));
  return client;
}

}  // namespace

int main(int argc, char** argv) {
  bool auth_only = false;
  bool sim = false;
  bool once = false;
  bool live = false;
  double poll_sec = 15.0;
  int max_tracks = 25;

  if (const char* v = std::getenv("MAX_TRACKS")) {
    max_tracks = std::atoi(v);
  }
  if (const char* v = std::getenv("POLL_SEC")) {
    poll_sec = std::atof(v);
  }

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      PrintUsage();
      return 0;
    }
    if (a == "--auth-only") {
      auth_only = true;
      continue;
    }
    if (a == "--sim") {
      sim = true;
      continue;
    }
    if (a == "--once") {
      once = true;
      continue;
    }
    if (a == "--poll" && i + 1 < argc) {
      poll_sec = std::atof(argv[++i]);
      live = true;
      continue;
    }
    if (a == "--max-tracks" && i + 1 < argc) {
      max_tracks = std::atoi(argv[++i]);
      continue;
    }
    std::cerr << "unknown arg: " << a << "\n";
    PrintUsage();
    return 2;
  }

  if (!auth_only && !sim) {
    live = true;
  }
  if (!auth_only && !sim && !live) {
    PrintUsage();
    return 2;
  }
  if (max_tracks < 1) {
    max_tracks = 1;
  }

  auto client = MakeClientFromEnv();
  const auto missing = client.MissingConfig();
  if (!missing.empty()) {
    std::cerr << "[olb] missing env:";
    for (const auto& m : missing) {
      std::cerr << " " << m;
    }
    std::cerr << "\n";
    return 1;
  }

  if (auth_only) {
    if (!client.FetchToken()) {
      std::cerr << "[olb] auth-only FAILED\n";
      return 1;
    }
    std::cerr << "[olb] auth-only OK endpoint=" << client.EndpointHost() << "\n";
    return 0;
  }

  olb::OpenSkyClient opensky;
  opensky.LoadBBoxFromEnv();

  do {
    std::vector<olb::AircraftState> states;
    if (sim) {
      states = olb::MakeSimAircraft(max_tracks > 3 ? 3 : max_tracks);
    } else {
      std::string err;
      states = opensky.FetchStates(&err);
      if (!err.empty()) {
        std::cerr << "[olb] " << err << "\n";
        if (once) {
          return 1;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(poll_sec * 1000)));
        continue;
      }
      if (static_cast<int>(states.size()) > max_tracks) {
        states.resize(static_cast<size_t>(max_tracks));
      }
    }

    int ok = 0;
    int fail = 0;
    for (const auto& ac : states) {
      const olb::PublishResult r = client.PublishAircraft(ac);
      if (r.ok) {
        ++ok;
        std::cerr << "[olb] HTTP " << r.status_code << " ok id=" << r.entity_id
                  << " callsign=" << (ac.callsign.empty() ? "-" : ac.callsign)
                  << " lat=" << ac.lat_deg << " lon=" << ac.lon_deg << "\n";
      } else {
        ++fail;
        std::cerr << "[olb] HTTP " << r.status_code << " FAIL id=" << r.entity_id
                  << " body=" << r.body.substr(0, 160) << "\n";
      }
    }
    std::cerr << "[olb] cycle done source=" << (sim ? "sim" : "opensky")
              << " n=" << states.size() << " ok=" << ok << " fail=" << fail
              << "\n";

    if (once) {
      return (ok > 0) ? 0 : 1;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(static_cast<int>(poll_sec * 1000)));
  } while (true);
}
