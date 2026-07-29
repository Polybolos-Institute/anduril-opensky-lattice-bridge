#pragma once

#include "aircraft.hpp"

#include <string>
#include <vector>

namespace olb {

/// Anonymous OpenSky /api/states/all poll (no account required).
class OpenSkyClient {
 public:
  void SetBBox(double lamin, double lomin, double lamax, double lomax);
  void LoadBBoxFromEnv();

  /// Returns airborne tracks. On failure, sets *error and returns empty.
  std::vector<AircraftState> FetchStates(std::string* error);

  [[nodiscard]] double Lamin() const { return lamin_; }
  [[nodiscard]] double Lomin() const { return lomin_; }
  [[nodiscard]] double Lamax() const { return lamax_; }
  [[nodiscard]] double Lomax() const { return lomax_; }

 private:
  double lamin_ = 32.5;
  double lomin_ = -97.3;
  double lamax_ = 33.1;
  double lomax_ = -96.4;
};

}  // namespace olb
