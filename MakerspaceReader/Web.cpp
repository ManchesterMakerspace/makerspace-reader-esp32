#include "Connectivity.h"
#include "ConnectivityPolicy.h"
#include "BuildInfo.h"
#include "Reader.h"
#include "secrets.h"
#include <ESPAsyncWebServer.h>
#include <ETH.h>
#include <ElegantOTA.h>
#include <LittleFS.h>
#include <esp_flash_encrypt.h>
#include <esp_secure_boot.h>
static AsyncWebServer server(80);
static std::atomic<uint32_t> otaActivity(0);
extern bool productionReady;
static String esc(String v) {
  v.replace("&", "&amp;");
  v.replace("<", "&lt;");
  v.replace(">", "&gt;");
  v.replace("\"", "&quot;");
  v.replace("'", "&#39;");
  return v;
}
static bool auth(AsyncWebServerRequest *r) {
  if (r->authenticate(WEB_USERNAME, WEB_PASSWORD))
    return true;
  r->requestAuthentication();
  return false;
}
static bool sameOrigin(AsyncWebServerRequest *r) {
  if (!r->hasHeader("Referer"))
    return false;
  String referer = r->header("Referer"), host = r->host();
  return referer.startsWith("https://" + host + "/") || referer.startsWith("http://" + host + "/");
}
// Called from the vendored ElegantOTA upload callback before any flash write.
bool readerOtaRequestAllowed(AsyncWebServerRequest *r) {
  if (!auth(r))
    return false;
  if (!sameOrigin(r) || !r->hasHeader("Cookie") ||
      r->header("Cookie").indexOf("reader_csrf=" + csrf) < 0) {
    r->send(403, "text/plain", "Same-origin update session required");
    return false;
  }
  if (r->url() == "/ota/start") {
    if ((r->hasParam("mode") && r->getParam("mode")->value() == "fs") || relayOn || maintenance) {
      r->send(409, "text/plain", "Firmware only; relay must be off and reader idle");
      return false;
    }
    if (!productionReady) {
      r->send(403, "text/plain", "Production security configuration required");
      return false;
    }
  }
  if (r->url() == "/ota/upload" && (!maintenance || !otaActivity)) {
    r->send(409, "text/plain", "Start an update session first");
    return false;
  }
  otaActivity = millis();
  return true;
}
static void page(AsyncWebServerRequest *r, const String &title, const String &content) {
  String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' "
                "content='width=device-width'><title>" +
                esc(title) +
                "</title><style>body{font:16px system-ui;max-width:1100px;margin:2em "
                "auto;padding:1em;color:#172536;background:#f4f7fa}nav{display:flex;gap:1em;flex-"
                "wrap:wrap}table{width:100%;border-collapse:collapse;background:white}td,th{"
                "padding:.6em;border-bottom:1px solid "
                "#ddd;text-align:left}input,select,button{padding:.5em;font:inherit}label{display:"
                "block;margin:1em 0}pre{white-space:pre-wrap}</style></head><body><nav><a "
                "href='/'>Home</a><a href='/config'>Configuration</a><a "
                "href='/stats'>Statistics</a><a href='/cache/cards'>Cards</a><a "
                "href='/cache/checkouts'>Checkouts</a><a href='/update'>Update</a></nav><h1>" +
                esc(title) + "</h1>" + content + "</body></html>";
  auto response = r->beginResponse(200, "text/html", html);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-Content-Type-Options", "nosniff");
  response->addHeader(
      "Content-Security-Policy",
      "default-src 'self'; style-src 'unsafe-inline'; frame-ancestors 'none'; form-action 'self'");
  r->send(response);
}
static String parameter(AsyncWebServerRequest *r, const char *key, bool post = false) {
  return r->hasParam(key, post) ? r->getParam(key, post)->value() : "";
}
void webSetup() {
  static_assert(sizeof(WEB_USERNAME) > 1 && sizeof(WEB_PASSWORD) > 16,
                "Nonempty username and long password required");
  server.addMiddleware([](AsyncWebServerRequest *r, ArMiddlewareNext next) {
    if (connectivity::provisioningPath(r->url().c_str()) &&
        !connectivity::manualProvisioningAllowed(wifiRadioActive(), ETH.linkUp() && ETH.hasIP() && uint32_t(ETH.localIP()) != 0,
          r->client() && r->client()->localIP() == ETH.localIP())) {
      r->send(403,"text/plain","Manual provisioning requires Ethernet with Wi-Fi fully off");
      return;
    }
    if (!auth(r))
      return;
    if (r->url() == "/update") {
      auto response = r->beginResponse(302, "text/plain", "");
      // An initial redirect installs an HttpOnly, SameSite cookie for the stock OTA UI.
      if (!r->hasHeader("Cookie") || r->header("Cookie").indexOf("reader_csrf=" + csrf) < 0) {
        response->addHeader("Set-Cookie",
                            "reader_csrf=" + csrf + "; Path=/; HttpOnly; SameSite=Strict");
        response->addHeader("Location", "/update");
        r->send(response);
        return;
      }
      delete response;
    }
    next();
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    Lock l(stateMutex);
    page(r, "Makerspace reader",
         "<p>" + esc(configuration["name"] | DEVICE_ID) + "</p><p>" +
             String(productionReady ? "Security profile ready"
                                    : "Production security not enabled; access denied") +
             "</p><p>" + esc(lastError) + "</p>");
  });
  auto stats = [](JsonDocument &d) {
    Lock s(stateMutex);
    d["name"] = configuration["name"];
    d["role"] = configuration["role"];
    d["deviceId"] = DEVICE_ID;
    buildStatistics(d);
    d["uptimeSeconds"] = millis() / 1000;
    d["freeHeap"] = ESP.getFreeHeap();
    d["freePSRAM"] = ESP.getFreePsram();
    d["ip"] = networkAddress();
    d["networkTransport"] = networkTransport();
    d["wifiRadioActive"] = wifiRadioActive();
    d["manualProvisioningAllowed"] = !wifiRadioActive() && ETH.linkUp() && ETH.hasIP() && uint32_t(ETH.localIP()) != 0;
    d["ethernet"] = ETH.linkUp() && ETH.hasIP() && uint32_t(ETH.localIP()) != 0;
    d["api"] = bool(apiOK);
    d["mqtt"] = bool(mqttOK);
    d["nfc"] = bool(nfcOK);
    d["nfcController"] = nfcControllerName();
    d["clockTrusted"] = bool(clockTrusted);
    d["relay"] = bool(relayOn);
    d["maintenance"] = bool(maintenance);
    d["storageHealthy"] = bool(storageOK);
    d["cacheCapacityExceeded"] = bool(cacheCapacityExceeded);
    d["cacheKeyReady"] = cacheKeyReady;
    d["acceptedSinceBoot"] = uint32_t(accepted);
    d["rejectedSinceBoot"] = uint32_t(rejected);
    d["auditLoss"] = uint32_t(dropped);
    d["cards"] = cards.size();
    d["checkouts"] = grants.size();
    d["flashUsed"] = LittleFS.usedBytes();
    d["flashTotal"] = LittleFS.totalBytes();
    d["lastError"] = lastError;
  };
  server.on("/api/local/stats", HTTP_GET, [stats](AsyncWebServerRequest *r) {
    JsonDocument d;
    stats(d);
    String out;
    serializeJson(d, out);
    r->send(200, "application/json", out);
  });
  server.on("/stats", HTTP_GET, [stats](AsyncWebServerRequest *r) {
    JsonDocument d;
    stats(d);
    String out = "<table>";
    for (JsonPair p : d.as<JsonObject>())
      out += "<tr><th>" + esc(p.key().c_str()) + "</th><td>" + esc(p.value().as<String>()) +
             "</td></tr>";
    page(r, "Statistics", out + "</table>");
  });
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *r) {
    Lock l(stateMutex);
    String out = "<form method='post' action='/config'><input type='hidden' name='csrf' value='" +
                 csrf + "'><label>Device name <input name='name' maxlength='48' required value='" +
                 esc(configuration["name"] | DEVICE_ID) +
                 "'></label><label>Role <select name='role'>";
    for (const char *v : {"door", "garagedoor", "test", "latch", "tool"})
      out += "<option " + String(configuration["role"] == v ? "selected" : "") + " value='" + v +
             "'>" + v + "</option>";
    out += "</select></label><label>Tool ID <input name='toolId' value='" +
           esc(configuration["toolId"] | "") +
           "'></label><label><input type='checkbox' name='enrollment' " +
           String(configuration["enrollmentEnabled"].as<bool>() ? "checked" : "") +
           "> Enrollment station</label><button>Save and synchronize</button></form>";
    out += "<form method='post' action='/enrollment/disable'><input type='hidden' name='csrf' "
           "value='" +
           csrf + "'><button>Disable enrollment locally now</button></form>";
    page(r, "Configuration", out);
  });
  auto csrfOK = [](AsyncWebServerRequest *r) {
    if (parameter(r, "csrf", true) != csrf) {
      r->send(403, "text/plain", "Invalid form token");
      return false;
    }
    return true;
  };
  server.on("/enrollment/disable", HTTP_POST, [csrfOK](AsyncWebServerRequest *r) {
    if (!csrfOK(r))
      return;
    if (!cacheKeyReady) {
      r->send(503, "text/plain", "Cache key unavailable; restore configuration and restart");
      return;
    }
    checkNvsWrite("enrollOff", prefs.putBool("enrollOff", true), 1);
    {
      Lock l(stateMutex);
      configuration["enrollmentEnabled"] = false;
    }
    saveState();
    r->redirect("/config");
  });
  server.on("/config", HTTP_POST, [csrfOK](AsyncWebServerRequest *r) {
    if (!csrfOK(r))
      return;
    if (!cacheKeyReady) {
      r->send(503, "text/plain", "Cache key unavailable; restore configuration and restart");
      return;
    }
    if (relayOn || maintenance) {
      r->send(409, "text/plain", "Stop relay before configuration changes");
      return;
    }
    JsonDocument d, response;
    d["name"] = parameter(r, "name", true);
    d["role"] = parameter(r, "role", true);
    d["toolId"] = parameter(r, "toolId", true);
    d["enrollmentEnabled"] = r->hasParam("enrollment", true);
    String body;
    serializeJson(d, body);
    maintenance = true;
    if (!api("/reader/v1/config", response, body)) {
      maintenance = false;
      r->send(503, "text/plain",
              "Configuration not changed: server unavailable or rejected request");
      return;
    }
    {
      Lock l(stateMutex);
      configurationSequence = response["_sequence"];
      configuration.set(response);
      configuration.remove("_sequence");
      configuration.remove("serverTime");
      configuration["toolAvailable"] = false;
      grants.clear();
    }
    checkNvsWrite("enrollOff", prefs.putBool("enrollOff", false), 1);
    saveState();
    maintenance = false;
    refreshNeeded = true;
    r->redirect("/config");
  });
  server.on("/cache/cards", HTTP_GET, [](AsyncWebServerRequest *r) {
    String q = parameter(r, "q");
    q.toLowerCase();
    int pageNo = std::clamp<long>(parameter(r, "page").toInt(), 0, 100000), index = 0;
    String out =
        "<form><input name='q' maxlength='96' placeholder='UID or "
        "holder'><button>Search</button></form><table><tr><th>UID</th><th>Holder</th><th>Validity</"
        "th><th>Expiry ms</th><th>FirstScan ms</th><th>Enrolled</th></tr>";
    {
      Lock l(stateMutex);
      std::vector<std::string> keys;
      for (auto &p : cards)
        keys.push_back(p.first);
      std::sort(keys.begin(), keys.end());
      for (auto &key : keys) {
        auto &c = cards.at(key);
        String search = c.uid + " " + c.holder;
        search.toLowerCase();
        if (q.length() && search.indexOf(q) < 0)
          continue;
        if (index++ / 25 != pageNo)
          continue;
        out += "<tr><td>" + esc(c.uid) + "</td><td>" + esc(c.holder) + "</td><td>" + esc(c.status) +
               "</td><td>" + String((long long)c.expiry) + "</td><td>" +
               (c.pending ? String("Time unavailable")
                : c.first ? String((long long)c.first)
                          : String("Never scanned")) +
               "</td><td>" + String(c.key.length() ? "Yes" : "No") + "</td></tr>";
      }
    }
    out += "</table><p>Page " + String(pageNo + 1) + "; " + String(index) +
           " matching records.</p><form><input type='hidden' name='q' value='" + esc(q) +
           "'><input type='number' name='page' min='0' value='" + String(pageNo + 1) +
           "'><button>Go to zero-based page</button></form>";
    page(r, "Cached cards", out);
  });
  server.on("/cache/checkouts", HTTP_GET, [](AsyncWebServerRequest *r) {
    Lock l(stateMutex);
    String q = parameter(r, "q");
    String out = "<form><input name='q' placeholder='Member "
                 "ID'><button>Search</button></form><table><tr><th>Member</th><th>Checkout</"
                 "th><th>Valid until ms</th></tr>";
    unsigned skip = std::clamp<long>(parameter(r, "page").toInt(), 0, 100000) * 25, count = 0;
    for (auto &p : grants) {
      auto &g = p.second;
      if (q.length() && g.member.indexOf(q) < 0)
        continue;
      if (count++ < skip || count > skip + 25)
        continue;
      out += "<tr><td>" + esc(g.member) + "</td><td>" + esc(g.id) + "</td><td>" +
             String((long long)g.until) + "</td></tr>";
    }
    page(r, "Cached checkouts", out + "</table><p>Use ?page=0,1,... for pages of 25.</p>");
  });
  ElegantOTA.begin(&server, WEB_USERNAME, WEB_PASSWORD);
  ElegantOTA.onStart([]() {
    {
      Lock s(stateMutex);
      maintenance = true;
      stopRelay();
    }
    saveState();
    otaActivity = millis();
  });
  ElegantOTA.onProgress([](size_t, size_t) { otaActivity = millis(); });
  ElegantOTA.onEnd([](bool ok) {
    if (!ok)
      maintenance = false;
    otaActivity = 0;
  });
  server.onNotFound([](AsyncWebServerRequest *r) { r->send(404, "text/plain", "Not found"); });
  server.begin();
}
void otaWatchdog() {
  if (otaActivity && millis() - otaActivity > 120000) {
    Update.abort();
    otaActivity = 0;
    maintenance = false;
  }
}
