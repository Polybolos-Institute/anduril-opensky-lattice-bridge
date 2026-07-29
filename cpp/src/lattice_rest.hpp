#pragma once

#include "aircraft.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace olb {

struct PublishResult {
  int status_code = 0;
  std::string body;
  bool ok = false;
  std::string entity_id;
};

/// Thin Lattice REST door: OAuth + Sandboxes Bearer + entity PUT (WinHTTP).
class LatticeRestClient {
 public:
  void SetEndpoint(std::string host_port);
  void SetCredentials(std::string client_id, std::string client_secret,
                      std::string env_token);

  [[nodiscard]] std::vector<std::string> MissingConfig() const;
  [[nodiscard]] const std::string& EndpointHost() const { return host_; }

  bool FetchToken();
  bool EnsureToken();
  [[nodiscard]] bool IsTokenValid() const;

  PublishResult PublishAircraft(const AircraftState& ac);

 private:
  std::string BuildEntityJson(const AircraftState& ac,
                              std::string* entity_id_out) const;

  std::string host_;
  std::string client_id_;
  std::string client_secret_;
  std::string env_token_;
  std::string access_token_;
  std::chrono::steady_clock::time_point token_expiry_{};
};

std::vector<AircraftState> MakeSimAircraft(int n = 3);

}  // namespace olb
