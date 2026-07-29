#include "opensky_client.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

namespace olb {
namespace {

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                    static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) {
    return {};
  }
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                      w.data(), n);
  return w;
}

bool ReadHttpResponseBody(HINTERNET hRequest, std::string* out) {
  out->clear();
  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
      return false;
    }
    if (avail == 0) {
      break;
    }
    std::vector<char> buf(static_cast<size_t>(avail));
    DWORD read = 0;
    if (!WinHttpReadData(hRequest, buf.data(),
                         static_cast<DWORD>(buf.size()), &read)) {
      return false;
    }
    out->append(buf.data(), read);
  }
  return true;
}

void SkipWs(const std::string& s, std::size_t* i) {
  while (*i < s.size() &&
         (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\r' || s[*i] == '\n')) {
    ++(*i);
  }
}

bool ParseJsonString(const std::string& s, std::size_t* i, std::string* out) {
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != '"') {
    return false;
  }
  ++(*i);
  out->clear();
  while (*i < s.size()) {
    const char c = s[*i];
    if (c == '"') {
      ++(*i);
      return true;
    }
    if (c == '\\' && *i + 1 < s.size()) {
      out->push_back(s[*i + 1]);
      *i += 2;
      continue;
    }
    out->push_back(c);
    ++(*i);
  }
  return false;
}

bool ParseJsonNull(const std::string& s, std::size_t* i) {
  SkipWs(s, i);
  if (s.compare(*i, 4, "null") == 0) {
    *i += 4;
    return true;
  }
  return false;
}

bool ParseJsonBool(const std::string& s, std::size_t* i, bool* out) {
  SkipWs(s, i);
  if (s.compare(*i, 4, "true") == 0) {
    *i += 4;
    *out = true;
    return true;
  }
  if (s.compare(*i, 5, "false") == 0) {
    *i += 5;
    *out = false;
    return true;
  }
  return false;
}

bool ParseJsonNumber(const std::string& s, std::size_t* i, double* out,
                     bool* is_null) {
  SkipWs(s, i);
  *is_null = false;
  if (ParseJsonNull(s, i)) {
    *is_null = true;
    *out = 0.0;
    return true;
  }
  char* end = nullptr;
  *out = std::strtod(s.c_str() + *i, &end);
  if (end == s.c_str() + *i) {
    return false;
  }
  *i = static_cast<std::size_t>(end - s.c_str());
  return true;
}

bool ParseStateRow(const std::string& s, std::size_t* i, AircraftState* ac) {
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != '[') {
    return false;
  }
  ++(*i);

  // 0 icao24 (string)
  std::string icao;
  if (!ParseJsonString(s, i, &icao)) {
    return false;
  }
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != ',') {
    return false;
  }
  ++(*i);

  // 1 callsign (string|null)
  std::string callsign;
  SkipWs(s, i);
  if (ParseJsonNull(s, i)) {
    callsign.clear();
  } else if (!ParseJsonString(s, i, &callsign)) {
    return false;
  }
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != ',') {
    return false;
  }
  ++(*i);

  // 2 origin_country
  std::string country;
  if (!ParseJsonString(s, i, &country)) {
    return false;
  }

  // skip 3 time_position, 4 last_contact
  for (int k = 0; k < 2; ++k) {
    SkipWs(s, i);
    if (*i >= s.size() || s[*i] != ',') {
      return false;
    }
    ++(*i);
    double tmp = 0;
    bool is_null = false;
    if (!ParseJsonNumber(s, i, &tmp, &is_null)) {
      return false;
    }
  }

  // 5 lon, 6 lat, 7 baro_alt, 8 on_ground, 9 velocity, 10 true_track
  double lon = 0, lat = 0, alt = 0, vel = 0, track = 0;
  bool lon_null = false, lat_null = false, alt_null = false, vel_null = false,
       track_null = false;
  bool on_ground = false;

  auto next_num = [&](double* v, bool* nnull) -> bool {
    SkipWs(s, i);
    if (*i >= s.size() || s[*i] != ',') {
      return false;
    }
    ++(*i);
    return ParseJsonNumber(s, i, v, nnull);
  };
  if (!next_num(&lon, &lon_null) || !next_num(&lat, &lat_null) ||
      !next_num(&alt, &alt_null)) {
    return false;
  }
  SkipWs(s, i);
  if (*i >= s.size() || s[*i] != ',') {
    return false;
  }
  ++(*i);
  if (!ParseJsonBool(s, i, &on_ground)) {
    // sometimes null
    if (!ParseJsonNull(s, i)) {
      return false;
    }
    on_ground = false;
  }
  if (!next_num(&vel, &vel_null) || !next_num(&track, &track_null)) {
    return false;
  }

  // consume until end of this row array
  int depth = 1;
  while (*i < s.size() && depth > 0) {
    const char c = s[*i];
    if (c == '"') {
      std::string ignore;
      if (!ParseJsonString(s, i, &ignore)) {
        return false;
      }
      continue;
    }
    if (c == '[') {
      ++depth;
    } else if (c == ']') {
      --depth;
    }
    ++(*i);
  }

  if (icao.empty() || lon_null || lat_null) {
    return false;
  }
  ac->icao24 = icao;
  // trim callsign
  while (!callsign.empty() && callsign.back() == ' ') {
    callsign.pop_back();
  }
  ac->callsign = callsign;
  ac->origin_country = country;
  ac->lon_deg = lon;
  ac->lat_deg = lat;
  if (!alt_null) {
    ac->alt_m = alt;
  }
  ac->on_ground = on_ground;
  if (!vel_null) {
    ac->speed_mps = vel;
  }
  if (!track_null) {
    ac->heading_deg = track;
  }
  ac->valid = true;
  ac->source = "opensky";
  return true;
}

std::vector<AircraftState> ParseStatesJson(const std::string& json) {
  std::vector<AircraftState> out;
  const std::size_t key = json.find("\"states\"");
  if (key == std::string::npos) {
    return out;
  }
  std::size_t i = json.find('[', key);
  if (i == std::string::npos) {
    return out;
  }
  ++i;  // inside states array
  for (;;) {
    SkipWs(json, &i);
    if (i >= json.size()) {
      break;
    }
    if (json[i] == ']') {
      break;
    }
    if (json[i] == ',') {
      ++i;
      continue;
    }
    if (json.compare(i, 4, "null") == 0) {
      break;
    }
    AircraftState ac;
    if (!ParseStateRow(json, &i, &ac)) {
      // skip to next top-level element best-effort
      break;
    }
    if (ac.valid && !ac.on_ground) {
      out.push_back(std::move(ac));
    }
  }
  return out;
}

}  // namespace

void OpenSkyClient::SetBBox(double lamin, double lomin, double lamax,
                            double lomax) {
  lamin_ = lamin;
  lomin_ = lomin;
  lamax_ = lamax;
  lomax_ = lomax;
}

void OpenSkyClient::LoadBBoxFromEnv() {
  if (const char* v = std::getenv("OPENSKY_LAMIN")) {
    lamin_ = std::atof(v);
  }
  if (const char* v = std::getenv("OPENSKY_LOMIN")) {
    lomin_ = std::atof(v);
  }
  if (const char* v = std::getenv("OPENSKY_LAMAX")) {
    lamax_ = std::atof(v);
  }
  if (const char* v = std::getenv("OPENSKY_LOMAX")) {
    lomax_ = std::atof(v);
  }
}

std::vector<AircraftState> OpenSkyClient::FetchStates(std::string* error) {
  if (error) {
    error->clear();
  }

  std::ostringstream path;
  path << "/api/states/all?lamin=" << lamin_ << "&lomin=" << lomin_
       << "&lamax=" << lamax_ << "&lomax=" << lomax_;
  const std::wstring wPath = Utf8ToWide(path.str());

  HINTERNET hSession =
      WinHttpOpen(L"opensky-lattice-bridge/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    if (error) {
      *error = "WinHttpOpen failed";
    }
    return {};
  }
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
  {
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                     sizeof(protocols));
  }

  HINTERNET hConnect =
      WinHttpConnect(hSession, L"opensky-network.org",
                     INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) {
    if (error) {
      *error = "WinHttpConnect opensky-network.org failed";
    }
    WinHttpCloseHandle(hSession);
    return {};
  }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"GET", wPath.c_str(), nullptr, WINHTTP_NO_REFERER, nullptr,
      WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    if (error) {
      *error = "WinHttpOpenRequest failed";
    }
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return {};
  }

  if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(hRequest, nullptr)) {
    if (error) {
      *error = "OpenSky HTTP request failed";
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return {};
  }

  DWORD status = 0;
  DWORD sz = sizeof(status);
  WinHttpQueryHeaders(hRequest,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                      WINHTTP_NO_HEADER_INDEX);
  std::string body;
  ReadHttpResponseBody(hRequest, &body);
  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);

  if (status == 429) {
    if (error) {
      *error = "opensky rate limited (429) - slow poll or add optional account";
    }
    return {};
  }
  if (status != 200) {
    if (error) {
      *error = "opensky HTTP " + std::to_string(status);
    }
    return {};
  }

  auto states = ParseStatesJson(body);
  std::cerr << "[olb] opensky fetched airborne=" << states.size()
            << " bbox=[" << lamin_ << "," << lomin_ << " .. " << lamax_ << ","
            << lomax_ << "]\n";
  return states;
}

}  // namespace olb
