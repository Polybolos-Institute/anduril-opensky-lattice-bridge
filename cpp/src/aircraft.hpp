#pragma once

#include <optional>
#include <string>

namespace olb {

struct AircraftState {
  std::string icao24;
  std::string callsign;
  std::string origin_country;
  double lat_deg = 0.0;
  double lon_deg = 0.0;
  std::optional<double> alt_m;
  double heading_deg = 0.0;
  double speed_mps = 0.0;
  bool on_ground = false;
  bool valid = false;
  std::string source = "opensky";
};

}  // namespace olb
