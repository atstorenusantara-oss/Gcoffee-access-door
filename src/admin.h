#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_timer.h>

constexpr int BUTTON_PIN = 23;
constexpr char AP_SSID[] = "Gcoffee-Admin";
#include "config.h"
constexpr unsigned long ADMIN_IDLE_MS = 300000, ENROLL_MS = 60000;
struct Employee { char name[41]; char card[11]; uint8_t active; };
Employee employees[5] = {};
Preferences prefs;
WebServer server(80);
esp_timer_handle_t doorTimer = nullptr;
esp_timer_handle_t buzzerTimer = nullptr;
bool storageReady = false, adminMode = false;
String sessionToken, csrfToken, loginToken, adminPin;
String notice = "Siap membaca kartu.", candidateCard;
unsigned long lastAdminMs = 0, lockStartMs = 0, enrollStartMs = 0;
bool loginLocked = false;
uint8_t failedLogins = 0;
int enrollSlot = -1;

String randomToken() {
  char token[33];
  snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
    (unsigned long)esp_random(), (unsigned long)esp_random(),
    (unsigned long)esp_random(), (unsigned long)esp_random());
  return String(token);
}
String escapeHtml(String value) {
  value.replace("&", "&amp;"); value.replace("<", "&lt;");
  value.replace(">", "&gt;"); value.replace("\"", "&quot;");
  value.replace("'", "&#39;"); return value;
}
bool validCard(const String& value) {
  if (value.length() != 10) return false;
  for (unsigned int i = 0; i < value.length(); ++i) {
    char c = value[i]; if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}
bool validPin(const String& value) {
  if (value.length() < 6 || value.length() > 12) return false;
  for (unsigned int i = 0; i < value.length(); ++i) if (value[i] < '0' || value[i] > '9') return false;
  return true;
}
void cancelEnrollment() { enrollSlot = -1; candidateCard = ""; }
bool saveEmployee(int slot, const Employee& record) {
  if (!storageReady) return false;
  String key = "emp" + String(slot + 1);
  if (prefs.putBytes(key.c_str(), &record, sizeof(record)) != sizeof(record)) return false;
  employees[slot] = record; return true;
}
void loadStorage() {
  storageReady = prefs.begin("gcoffee", false); adminPin = INITIAL_PIN;
  if (!storageReady) { Serial.println("NVS gagal: akses kartu dinonaktifkan."); return; }
  adminPin = prefs.getString("pin", INITIAL_PIN);
  if (!validPin(adminPin)) adminPin = INITIAL_PIN;
  for (int i = 0; i < 5; ++i) {
    snprintf(employees[i].name, sizeof(employees[i].name), "Karyawan %d", i + 1);
    String key = "emp" + String(i + 1);
    if (prefs.isKey(key.c_str())) {
      Employee saved = {};
      if (prefs.getBytesLength(key.c_str()) == sizeof(saved) &&
          prefs.getBytes(key.c_str(), &saved, sizeof(saved)) == sizeof(saved) &&
          saved.name[40] == '\0' && saved.card[10] == '\0' && saved.active <= 1 &&
          (!saved.card[0] || validCard(String(saved.card)))) employees[i] = saved;
      else Serial.printf("Record K%03d rusak: akses ditolak.\n", i + 1);
    } else {
      Employee initial = employees[i];
      if (i == 0) { strcpy(initial.card, "4A00A93465"); initial.active = 1; }
      if (!saveEmployee(i, initial)) Serial.println("Gagal menyimpan data awal.");
    }
  }
}
void closeDoor(void*) {
  digitalWrite(RELAY_PIN, HIGH); digitalWrite(LED_BUTTON_PIN, HIGH);
}
void stopBuzzer(void*) { digitalWrite(BUZZER_PIN, HIGH); }
void beep(unsigned long durationMs) {
  if (!buzzerTimer) return;
  esp_timer_stop(buzzerTimer);
  digitalWrite(BUZZER_PIN, LOW);
  if (esp_timer_start_once(buzzerTimer, durationMs * 1000ULL) != ESP_OK) stopBuzzer(nullptr);
}
bool openDoor() {
  if (!doorTimer) return false;
  esp_timer_stop(doorTimer);
  digitalWrite(RELAY_PIN, LOW); digitalWrite(LED_BUTTON_PIN, LOW);
  if (esp_timer_start_once(doorTimer, ACTION_DURATION_MS * 1000ULL) != ESP_OK) {
    closeDoor(nullptr); return false;
  }
  return true;
}
void noCache() {
  server.sendHeader("Cache-Control", "no-store"); server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("X-Content-Type-Options", "nosniff");
}
void redirectHome() { server.sendHeader("Location", "/"); server.send(303, "text/plain", ""); }
String page(const String& title, const String& body) {
  return "<!doctype html><html lang='id'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Gcoffee Admin</title><style>body{font:16px system-ui;background:#f5f1e9;color:#29251f;margin:0}main{max-width:850px;margin:auto;padding:24px}"
    "h1{font-size:28px}section{background:white;padding:20px;border-radius:14px;margin:16px 0}input,button{font:inherit;padding:10px;margin:5px 0;box-sizing:border-box}"
    "input{max-width:100%;border:1px solid #bbb;border-radius:6px}button{background:#315c46;color:white;border:0;border-radius:6px;cursor:pointer}"
    ".muted{color:#625c53}.notice{padding:12px;background:#e6eddf;border-radius:8px}label{display:block}form{margin:8px 0}</style>"
    "<main><p class='muted'>GCOFFEE / AKSES PINTU</p><h1>" + title + "</h1>" + body + "</main></html>";
}
String postForm(const String& path) {
  return "<form method='post' action='" + path + "'><input type='hidden' name='csrf' value='" + csrfToken + "'>";
}
bool sessionValid() {
  return adminMode && !sessionToken.isEmpty() && millis() - lastAdminMs < ADMIN_IDLE_MS &&
    server.header("Cookie") == "GCSESSION=" + sessionToken;
}
bool requireAdmin(bool mutation = false) {
  noCache();
  if (!sessionValid()) { server.send(401, "text/plain", "Sesi berakhir. Buka / dan login kembali."); return false; }
  if (mutation && server.arg("csrf") != csrfToken) { server.send(403, "text/plain", "Permintaan tidak valid."); return false; }
  // Poll status tidak memperpanjang mode admin.
  if (mutation || server.uri() == "/") lastAdminMs = millis();
  return true;
}
int requestSlot() {
  String id = server.arg("slot");
  return id.length() == 1 && id[0] >= '1' && id[0] <= '5' ? id[0] - '1' : -1;
}
#include "attendance.h"
#include "firmware_update.h"
void showHome() {
  noCache();
  if (!sessionValid()) {
    String body = "<section><p>Masukkan PIN admin untuk melanjutkan.</p><form method='post' action='/login'>"
      "<input type='hidden' name='token' value='" + loginToken + "'><label>PIN admin</label>"
      "<input name='pin' type='password' inputmode='numeric' minlength='6' maxlength='12' required autocomplete='current-password'>"
      "<br><button>Masuk</button></form></section>";
    server.send(200, "text/html; charset=utf-8", page("Login admin", body)); return;
  }
  lastAdminMs = millis();
  String body = "<p class='notice' id='status'>" + escapeHtml(notice) + "</p>";
  if (!storageReady) body += "<p>NVS gagal. Perubahan data tidak dapat disimpan.</p>";
  body += attendancePanel();
  body += firmwarePanel();
  body += "<section><h2>Kontrol pintu</h2>" + postForm("/open") + "<button>Buka pintu 3 detik</button></form>"
    "<p class='muted'>Relay GPIO4 dan LED GPIO15 aktif LOW.</p></section>";
  for (int i = 0; i < 5; ++i) {
    Employee& e = employees[i];
    body += "<section><h2>K00" + String(i + 1) + " — " + escapeHtml(e.name) + "</h2><p>Kartu: <b>" +
      (e.card[0] ? String(e.card) : String("Belum didaftarkan")) + "</b> · " + (e.active ? "Aktif" : "Nonaktif") + "</p>";
    String slot = "<input type='hidden' name='slot' value='" + String(i + 1) + "'>";
    body += postForm("/employee") + slot + "<label>Nama karyawan</label><input name='name' maxlength='40' value='" + escapeHtml(e.name) + "' required>"
      "<label><input type='checkbox' name='active' value='1' " + String(e.active ? "checked" : "") + "> Akses kartu aktif</label><button>Simpan nama / status</button></form>";
    body += postForm("/enroll") + slot + "<button>" + String(e.card[0] ? "Ganti kartu" : "Daftarkan kartu") + "</button></form></section>";
  }
  body += "<section><h2>Pendaftaran kartu</h2><p>Pilih karyawan, lalu tempelkan kartu. Batas waktu 60 detik. Setelah kartu terbaca, tekan Simpan kartu.</p>" +
    postForm("/commit") + "<button>Simpan kartu yang terbaca</button></form>" + postForm("/cancel") + "<button>Batalkan pendaftaran</button></form></section>";
  body += "<section><h2>Ganti PIN</h2>" + postForm("/pin") + "<label>PIN saat ini</label><input name='old' type='password' required>"
    "<label>PIN baru (6–12 angka)</label><input name='pin' type='password' pattern='[0-9]{6,12}' required>"
    "<label>Ulangi PIN baru</label><input name='confirm' type='password' required><br><button>Simpan PIN baru</button></form></section>";
  body += postForm("/logout") + "<button>Keluar</button></form>" + postForm("/stop") + "<button>Matikan mode admin / WiFi</button></form>"
    "<p class='muted'>Mode admin mati setelah 5 menit tanpa aktivitas.</p>"
    "<script>setInterval(async()=>{try{let r=await fetch('/status',{cache:'no-store'});if(r.status===401){location.href='/';return;}"
    "if(r.ok)document.getElementById('status').textContent=await r.text();}catch(e){}},2000);</script>";
  server.send(200, "text/html; charset=utf-8", page("Dashboard admin", body));
}
void stopAdmin() {
  cancelEnrollment(); sessionToken = ""; csrfToken = "";
  server.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); adminMode = false;
  Serial.println("Mode admin dan WiFi dimatikan.");
}
void startAdmin() {
  if (adminMode) { lastAdminMs = millis(); return; }
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 1, false, 1)) { WiFi.mode(WIFI_OFF); Serial.println("Gagal memulai AP."); return; }
  loginToken = randomToken(); sessionToken = ""; csrfToken = "";
  adminMode = true; lastAdminMs = millis(); server.begin();
  Serial.print("Mode admin aktif. Buka http://"); Serial.println(WiFi.softAPIP());
}
void setupRoutes() {
  const char* headers[] = {"Cookie", "X-CSRF-Token", "X-Firmware-Size", "X-Firmware-SHA256"}; server.collectHeaders(headers, 4);
  server.on("/firmware", HTTP_POST, finishFirmware, receiveFirmware);
  server.on("/", HTTP_GET, showHome);
  server.on("/login", HTTP_POST, [] {
    noCache();
    if (server.arg("token") != loginToken) { server.send(403, "text/plain", "Buka halaman login kembali."); return; }
    if (loginLocked && millis() - lockStartMs < 60000) { server.send(429, "text/plain", "Terlalu banyak percobaan. Tunggu 60 detik."); return; }
    if (loginLocked) { loginLocked = false; failedLogins = 0; }
    if (server.arg("pin") != adminPin) {
      if (++failedLogins >= 5) { loginLocked = true; lockStartMs = millis(); }
      server.send(401, "text/html; charset=utf-8", page("PIN salah", "<a href='/'>Coba lagi</a>")); return;
    }
    failedLogins = 0; sessionToken = randomToken(); csrfToken = randomToken();
    cancelEnrollment(); lastAdminMs = millis();
    server.sendHeader("Set-Cookie", "GCSESSION=" + sessionToken + "; Path=/; HttpOnly; SameSite=Strict"); redirectHome();
  });
  server.on("/status", HTTP_GET, [] {
    if (!requireAdmin()) return;
    String text = notice;
    text += " | " + clockStatus();
    if (!logError.isEmpty()) text += " | " + logError;
    if (enrollSlot >= 0) text += " | K00" + String(enrollSlot + 1) + ": " +
      (candidateCard.isEmpty() ? "Menunggu kartu..." : "Kartu " + candidateCard + " siap. Tekan Simpan kartu.");
    server.send(200, "text/plain; charset=utf-8", text);
  });
  server.on("/open", HTTP_POST, [] {
    if (!requireAdmin(true)) return;
    bool opened = openDoor();
    notice = opened ? "Pintu dibuka manual selama 3 detik." : "Gagal mengaktifkan relay.";
    if (opened && !appendLog(attendance::MANUAL, 0)) notice += " Log gagal disimpan.";
    Serial.println(notice); redirectHome();
  });
  server.on("/employee", HTTP_POST, [] {
    if (!requireAdmin(true)) return;
    int slot = requestSlot(); String name = server.arg("name"); name.trim();
    if (slot < 0 || name.isEmpty() || name.length() > 40) { server.send(400, "text/plain", "Nama / karyawan tidak valid (maksimal 40 byte)."); return; }
    Employee next = employees[slot]; name.toCharArray(next.name, sizeof(next.name));
    next.active = server.hasArg("active") && next.card[0];
    notice = saveEmployee(slot, next) ? "Data karyawan disimpan." : "Gagal menyimpan NVS."; redirectHome();
  });
  server.on("/enroll", HTTP_POST, [] {
    if (!requireAdmin(true)) return;
    int slot = requestSlot(); if (slot < 0 || !storageReady) { server.send(400, "text/plain", "Karyawan / NVS tidak tersedia."); return; }
    enrollSlot = slot; candidateCard = ""; enrollStartMs = millis();
    notice = "Tempelkan kartu untuk K00" + String(slot + 1) + ". Saat pendaftaran, scan tidak membuka pintu."; redirectHome();
  });
  server.on("/commit", HTTP_POST, [] {
    if (!requireAdmin(true)) return;
    if (enrollSlot < 0 || millis() - enrollStartMs >= ENROLL_MS || !validCard(candidateCard)) {
      notice = "Belum ada kartu atau waktu pendaftaran habis."; redirectHome(); return;
    }
    for (int i = 0; i < 5; ++i) if (i != enrollSlot && candidateCard == employees[i].card) {
      notice = "Kartu sudah dimiliki karyawan lain."; redirectHome(); return;
    }
    Employee next = employees[enrollSlot]; candidateCard.toCharArray(next.card, sizeof(next.card)); next.active = 1;
    if (saveEmployee(enrollSlot, next)) { notice = "Kartu baru tersimpan. Kartu sebelumnya tidak berlaku."; cancelEnrollment(); }
    else notice = "Gagal menyimpan. Kartu sebelumnya tetap berlaku.";
    redirectHome();
  });
  server.on("/cancel", HTTP_POST, [] { if (!requireAdmin(true)) return; cancelEnrollment(); notice = "Pendaftaran dibatalkan."; redirectHome(); });
  server.on("/pin", HTTP_POST, [] {
    if (!requireAdmin(true)) return;
    String pin = server.arg("pin");
    if (server.arg("old") != adminPin || !validPin(pin) || pin != server.arg("confirm")) { notice = "PIN lama / konfirmasi salah, atau PIN baru bukan 6–12 angka."; redirectHome(); return; }
    if (!storageReady || prefs.putString("pin", pin) != pin.length()) { notice = "Gagal menyimpan PIN."; redirectHome(); return; }
    adminPin = pin; cancelEnrollment(); sessionToken = ""; csrfToken = "";
    notice = "PIN berhasil diganti. Silakan login kembali."; redirectHome();
  });
  server.on("/logout", HTTP_POST, [] {
    if (!requireAdmin(true)) return;
    cancelEnrollment(); sessionToken = ""; csrfToken = "";
    server.sendHeader("Set-Cookie", "GCSESSION=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict"); redirectHome();
  });
  server.on("/stop", HTTP_POST, [] {
    if (!requireAdmin(true)) return;
    server.send(200, "text/plain", "Mode admin dimatikan. Tahan tombol 3 detik untuk mengaktifkan kembali."); stopAdmin();
  });
  server.onNotFound([] { noCache(); server.send(404, "text/plain", "Halaman tidak ditemukan."); });
}
void handleValidCard(const String& card) {
  if (firmwareReceiving || firmwareReboot) return;
  if (enrollSlot >= 0 && millis() - enrollStartMs >= ENROLL_MS) {
    cancelEnrollment(); notice = "Waktu pendaftaran habis.";
    return; // Scan yang tiba di batas waktu tidak membuka pintu.
  }
  if (enrollSlot >= 0) {
    if (candidateCard.isEmpty()) { candidateCard = card; notice = "Kartu terbaca, menunggu konfirmasi Simpan kartu."; }
    return;
  }
  if (storageReady) for (int i = 0; i < 5; ++i) {
    if (employees[i].active && card == employees[i].card) {
      beep(BEEP_ACCEPT_MS);
      bool opened = openDoor();
      notice = opened ? "Akses diterima: K00" + String(i + 1) + " — " + employees[i].name : "Gagal mengaktifkan relay.";
      if (!logEmployeeScan(i, card)) notice += " Log gagal disimpan.";
      if (!currentEpoch()) notice += " Jam belum sinkron; waktu absen belum tersedia.";
      Serial.println(notice); return;
    }
  }
  beep(BEEP_REJECT_MS);
  notice = "Akses ditolak: kartu tidak terdaftar / nonaktif."; Serial.println(notice);
}
void setupAdmin() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  esp_timer_create_args_t args = {}; args.callback = closeDoor; args.name = "door-close";
  if (esp_timer_create(&args, &doorTimer) != ESP_OK) doorTimer = nullptr;
  args.callback = stopBuzzer; args.name = "buzzer-off";
  if (esp_timer_create(&args, &buzzerTimer) != ESP_OK) {
    buzzerTimer = nullptr;
    Serial.println("Timer buzzer gagal dibuat; buzzer dinonaktifkan.");
  }
  loadStorage(); setupAttendance(); setupRoutes(); setupAttendanceRoutes(); WiFi.mode(WIFI_OFF);
  Serial.println("Tahan tombol GPIO23 selama 3 detik untuk WiFi admin.");
}
void pollAdmin() {
  pollFirmware();
  static bool rawLast = HIGH, stable = HIGH, handled = false;
  static unsigned long changedMs = 0, pressedMs = 0;
  bool raw = digitalRead(BUTTON_PIN); unsigned long now = millis();
  if (raw != rawLast) { rawLast = raw; changedMs = now; }
  if (now - changedMs >= 30 && stable != raw) {
    stable = raw; if (stable == LOW) { pressedMs = now; handled = false; }
  }
  if (stable == LOW && !handled && now - pressedMs >= 3000) { handled = true; startAdmin(); }
  if (adminMode) {
    if (millis() - lastAdminMs >= ADMIN_IDLE_MS) stopAdmin();
    else {
      if (enrollSlot >= 0 && millis() - enrollStartMs >= ENROLL_MS) { cancelEnrollment(); notice = "Waktu pendaftaran habis."; }
      server.handleClient();
    }
  }
}
