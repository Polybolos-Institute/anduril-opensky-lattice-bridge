#include "lattice_rest.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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

std::string FormEncode(const std::string& s) {
  std::ostringstream o;
  for (unsigned char c : s) {
    if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
      o << static_cast<char>(c);
    } else {
      static const char* kHex = "0123456789ABCDEF";
      o << '%' << kHex[c >> 4] << kHex[c & 0x0F];
    }
  }
  return o.str();
}

bool SplitHostPort(const std::string& target, std::string* host,
                   INTERNET_PORT* port) {
  if (!host || !port || target.empty()) {
    return false;
  }
  std::string t = target;
  for (const char* prefix : {"https://", "http://"}) {
    const std::size_t n = std::char_traits<char>::length(prefix);
    if (t.size() >= n && t.compare(0, n, prefix) == 0) {
      t = t.substr(n);
      break;
    }
  }
  while (!t.empty() && t.back() == '/') {
    t.pop_back();
  }
  const std::size_t colon = t.rfind(':');
  if (colon != std::string::npos && colon > 0) {
    const int p = std::atoi(t.c_str() + colon + 1);
    if (p > 0 && p < 65536) {
      *host = t.substr(0, colon);
      *port = static_cast<INTERNET_PORT>(p);
      return true;
    }
  }
  *host = t;
  *port = INTERNET_DEFAULT_HTTPS_PORT;
  return true;
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

bool ExtractJsonStringField(const std::string& json, const char* key,
                            std::string* out) {
  const std::string pat = std::string("\"") + key + "\":\"";
  const std::size_t p = json.find(pat);
  if (p == std::string::npos) {
    return false;
  }
  std::size_t i = p + pat.size();
  std::string s;
  while (i < json.size()) {
    if (json[i] == '"') {
      break;
    }
    if (json[i] == '\\' && i + 1 < json.size()) {
      s.push_back(json[i + 1]);
      i += 2;
      continue;
    }
    s.push_back(json[i]);
    ++i;
  }
  *out = std::move(s);
  return !out->empty();
}

bool ExtractJsonIntField(const std::string& json, const char* key, int* out) {
  const std::string pat = std::string("\"") + key + "\":";
  std::size_t p = json.find(pat);
  if (p == std::string::npos) {
    return false;
  }
  p += pat.size();
  while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) {
    ++p;
  }
  *out = static_cast<int>(std::strtol(json.c_str() + p, nullptr, 10));
  return true;
}

std::string Rfc3339Utc(std::time_t t) {
  std::tm tm{};
  gmtime_s(&tm, &t);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec);
  return buf;
}

std::string JsonEscape(const std::string& s) {
  std::ostringstream o;
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        o << "\\\"";
        break;
      case '\\':
        o << "\\\\";
        break;
      case '\n':
        o << "\\n";
        break;
      case '\r':
        o << "\\r";
        break;
      case '\t':
        o << "\\t";
        break;
      default:
        if (c < 0x20) {
          char hex[8];
          std::snprintf(hex, sizeof(hex), "\\u%04x", c);
          o << hex;
        } else {
          o << static_cast<char>(c);
        }
        break;
    }
  }
  return o.str();
}

void ApplyTls12(HINTERNET hSession) {
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif
  DWORD protocols =
      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
  WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                   sizeof(protocols));
}

}  // namespace

void LatticeRestClient::SetEndpoint(std::string host_port) {
  host_ = std::move(host_port);
}

void LatticeRestClient::SetCredentials(std::string client_id,
                                       std::string client_secret,
                                       std::string env_token) {
  client_id_ = std::move(client_id);
  client_secret_ = std::move(client_secret);
  env_token_ = std::move(env_token);
  access_token_.clear();
  token_expiry_ = {};
}

std::vector<std::string> LatticeRestClient::MissingConfig() const {
  std::vector<std::string> missing;
  if (host_.empty()) {
    missing.emplace_back("LATTICE_ENDPOINT");
  }
  if (client_id_.empty()) {
    missing.emplace_back("LATTICE_CLIENT_ID");
  }
  if (client_secret_.empty()) {
    missing.emplace_back("LATTICE_CLIENT_SECRET");
  }
  if (env_token_.empty()) {
    missing.emplace_back("LATTICE_ENV_TOKEN");
  }
  return missing;
}

bool LatticeRestClient::IsTokenValid() const {
  if (access_token_.empty()) {
    return false;
  }
  return std::chrono::steady_clock::now() < token_expiry_;
}

bool LatticeRestClient::FetchToken() {
  if (client_id_.empty() || client_secret_.empty() || env_token_.empty() ||
      host_.empty()) {
    std::cerr << "[olb] FetchToken: missing credential or endpoint\n";
    return false;
  }

  std::string oauth_host;
  INTERNET_PORT oauth_port = INTERNET_DEFAULT_HTTPS_PORT;
  if (!SplitHostPort(host_, &oauth_host, &oauth_port) || oauth_host.empty()) {
    std::cerr << "[olb] FetchToken: bad endpoint\n";
    return false;
  }

  const std::string body =
      std::string("grant_type=client_credentials&client_id=") +
      FormEncode(client_id_) + "&client_secret=" + FormEncode(client_secret_);

  std::cerr << "[olb] FetchToken: URL=https://" << oauth_host
            << "/api/v1/oauth/token\n";

  HINTERNET hSession =
      WinHttpOpen(L"opensky-lattice-bridge/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    return false;
  }
  ApplyTls12(hSession);

  HINTERNET hConnect =
      WinHttpConnect(hSession, Utf8ToWide(oauth_host).c_str(),
                     INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) {
    WinHttpCloseHandle(hSession);
    return false;
  }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"POST", L"/api/v1/oauth/token", nullptr, WINHTTP_NO_REFERER,
      nullptr, WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  const std::string extra_headers =
      "Content-Type: application/x-www-form-urlencoded\r\n"
      "Anduril-Sandbox-Authorization: Bearer " +
      env_token_ + "\r\n";
  const std::wstring wExtra = Utf8ToWide(extra_headers);
  if (!WinHttpAddRequestHeaders(hRequest, wExtra.c_str(),
                                static_cast<DWORD>(wExtra.size()),
                                WINHTTP_ADDREQ_FLAG_ADD)) {
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  const BOOL sent = WinHttpSendRequest(
      hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
      reinterpret_cast<LPVOID>(const_cast<char*>(body.data())),
      static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
  if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
    std::cerr << "[olb] FetchToken: HTTP request failed gle=" << GetLastError()
              << "\n";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  DWORD status = 0;
  DWORD sz = sizeof(status);
  WinHttpQueryHeaders(hRequest,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                      WINHTTP_NO_HEADER_INDEX);
  std::string resp;
  ReadHttpResponseBody(hRequest, &resp);
  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);

  std::cerr << "[olb] FetchToken: HTTP " << status << "\n";
  if (status != 200) {
    return false;
  }

  std::string token;
  if (!ExtractJsonStringField(resp, "access_token", &token)) {
    return false;
  }
  int expires_in = 1800;
  ExtractJsonIntField(resp, "expires_in", &expires_in);
  if (expires_in < 60) {
    expires_in = 60;
  }
  access_token_ = std::move(token);
  token_expiry_ = std::chrono::steady_clock::now() +
                  std::chrono::seconds(expires_in - 30);
  std::cerr << "[olb] FetchToken: access_token acquired expires_in="
            << expires_in << "s\n";
  return true;
}

bool LatticeRestClient::EnsureToken() {
  if (IsTokenValid()) {
    return true;
  }
  if (FetchToken()) {
    return true;
  }
  if (env_token_.empty()) {
    return false;
  }
  access_token_ = env_token_;
  token_expiry_ = std::chrono::steady_clock::now() + std::chrono::hours(1);
  std::cerr << "[olb] EnsureToken: OAuth unavailable - using sandbox JWT as "
               "Bearer\n";
  return true;
}

std::string LatticeRestClient::BuildEntityJson(const AircraftState& ac,
                                               std::string* entity_id_out) const {
  const std::time_t now = std::time(nullptr);
  const std::string created = Rfc3339Utc(now);
  const std::string expiry = Rfc3339Utc(now + 120);
  std::string name = ac.callsign.empty() ? ac.icao24 : ac.callsign;
  while (!name.empty() && name.back() == ' ') {
    name.pop_back();
  }
  if (name.empty()) {
    name = ac.icao24;
  }
  std::string icao = ac.icao24;
  for (char& c : icao) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  const std::string entity_id = "polybolos-adsb-" + icao;
  if (entity_id_out) {
    *entity_id_out = entity_id;
  }

  char lat[64], lon[64], alt[64];
  std::snprintf(lat, sizeof(lat), "%.8f", ac.lat_deg);
  std::snprintf(lon, sizeof(lon), "%.8f", ac.lon_deg);
  const double alt_m = ac.alt_m.has_value() ? *ac.alt_m : 0.0;
  std::snprintf(alt, sizeof(alt), "%.3f", alt_m);

  const std::string country =
      ac.origin_country.empty() ? "UNK" : ac.origin_country;

  std::ostringstream o;
  o << "{"
    << "\"entityId\":\"" << JsonEscape(entity_id) << "\","
    << "\"description\":\"OpenSky ADS-B " << JsonEscape(name) << " ("
    << JsonEscape(country) << ")\","
    << "\"isLive\":true,"
    << "\"createdTime\":\"" << created << "\","
    << "\"expiryTime\":\"" << expiry << "\","
    << "\"aliases\":{\"name\":\"" << JsonEscape(name) << "\"},"
    << "\"milView\":{"
    << "\"disposition\":\"DISPOSITION_UNKNOWN\","
    << "\"environment\":\"ENVIRONMENT_AIR\"},"
    << "\"location\":{\"position\":{"
    << "\"latitudeDegrees\":" << lat << ","
    << "\"longitudeDegrees\":" << lon << ","
    << "\"altitudeHaeMeters\":" << alt << "}},"
    << "\"ontology\":{"
    << "\"template\":\"TEMPLATE_TRACK\","
    << "\"platformType\":\"ADS-B AIRPLANE\"},"
    << "\"provenance\":{"
    << "\"dataType\":\"adsb\","
    << "\"integrationName\":\"polybolos-opensky-lattice-bridge\","
    << "\"sourceUpdateTime\":\"" << created << "\"},"
    << "\"dataClassification\":{"
    << "\"default\":{\"level\":\"CLASSIFICATION_LEVELS_UNCLASSIFIED\"}}"
    << "}";
  return o.str();
}

PublishResult LatticeRestClient::PublishAircraft(const AircraftState& ac) {
  PublishResult result;
  if (!ac.valid) {
    result.body = "aircraft state is not valid";
    return result;
  }
  if (!EnsureToken()) {
    result.body = "EnsureToken failed";
    return result;
  }

  std::string endpoint_host;
  INTERNET_PORT endpoint_port = INTERNET_DEFAULT_HTTPS_PORT;
  if (!SplitHostPort(host_, &endpoint_host, &endpoint_port) ||
      endpoint_host.empty() || access_token_.empty()) {
    result.body = "missing endpoint or token";
    return result;
  }

  std::string entity_id;
  const std::string body = BuildEntityJson(ac, &entity_id);
  result.entity_id = entity_id;

  HINTERNET hSession =
      WinHttpOpen(L"opensky-lattice-bridge/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) {
    result.body = "WinHttpOpen failed";
    return result;
  }
  ApplyTls12(hSession);

  HINTERNET hConnect =
      WinHttpConnect(hSession, Utf8ToWide(endpoint_host).c_str(),
                     INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) {
    result.body = "WinHttpConnect failed";
    WinHttpCloseHandle(hSession);
    return result;
  }

  HINTERNET hRequest = WinHttpOpenRequest(
      hConnect, L"PUT", L"/api/v1/entities", nullptr, WINHTTP_NO_REFERER,
      nullptr, WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    result.body = "WinHttpOpenRequest failed";
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
  }

  const std::string headers =
      "Content-Type: application/json\r\n"
      "Authorization: Bearer " +
      access_token_ +
      "\r\n"
      "Anduril-Sandbox-Authorization: Bearer " +
      env_token_ + "\r\n";
  const std::wstring wHeaders = Utf8ToWide(headers);
  if (!WinHttpAddRequestHeaders(hRequest, wHeaders.c_str(),
                                static_cast<DWORD>(wHeaders.size()),
                                WINHTTP_ADDREQ_FLAG_ADD)) {
    result.body = "WinHttpAddRequestHeaders failed";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
  }

  const BOOL sent = WinHttpSendRequest(
      hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
      reinterpret_cast<LPVOID>(const_cast<char*>(body.data())),
      static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
  if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
    result.body = "HTTP request failed";
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
  }

  DWORD status = 0;
  DWORD sz = sizeof(status);
  WinHttpQueryHeaders(hRequest,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                      WINHTTP_NO_HEADER_INDEX);
  std::string resp;
  ReadHttpResponseBody(hRequest, &resp);
  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);

  result.status_code = static_cast<int>(status);
  result.body = std::move(resp);
  result.ok = (status >= 200 && status < 300);
  return result;
}

std::vector<AircraftState> MakeSimAircraft(int n) {
  if (n < 1) {
    n = 1;
  }
  if (n > 25) {
    n = 25;
  }
  const double t = static_cast<double>(std::time(nullptr));
  constexpr double lat0 = 32.8990;
  constexpr double lon0 = -97.0403;
  std::vector<AircraftState> out;
  out.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double ang =
        std::fmod(t / 40.0 + i * (2.0 * 3.14159265358979323846 / n),
                  2.0 * 3.14159265358979323846);
    AircraftState ac;
    char icao[16];
    std::snprintf(icao, sizeof(icao), "sim%03d", i);
    ac.icao24 = icao;
    char cs[16];
    std::snprintf(cs, sizeof(cs), "SIM%03d", i + 1);
    ac.callsign = cs;
    ac.origin_country = "United States";
    ac.lat_deg = lat0 + 0.08 * std::cos(ang + i);
    ac.lon_deg = lon0 + 0.10 * std::sin(ang + i);
    ac.alt_m = 3000.0 + 250.0 * i;
    ac.heading_deg =
        std::fmod(ang * 180.0 / 3.14159265358979323846 + 90.0, 360.0);
    ac.speed_mps = 120.0;
    ac.on_ground = false;
    ac.valid = true;
    ac.source = "sim";
    out.push_back(std::move(ac));
  }
  return out;
}

}  // namespace olb
