#pragma once
#include <LittleFS.h>
#include <esp_partition.h>
#include <sys/time.h>
#include <stddef.h>
#include "attendance_logic.h"

// Record tetap + checksum: record terpotong akibat listrik padam diabaikan.
struct LogRecord {
  uint64_t uptimeMs;
  uint32_t magic, epoch, bootId;
  uint8_t slot, kind;
  char name[41], card[11];
  uint8_t reserved[2];
  uint32_t checksum;
};
static_assert(sizeof(LogRecord) == 80, "Format log harus tetap 80 byte");
constexpr uint32_t LOG_MAGIC = 0x47434631;
constexpr size_t MAX_LOG_BYTES = 1024 * 1024;
constexpr char LOG_PATH[] = "/events-v1.bin";
bool logsReady = false, clockSynced = false;
uint32_t bootId = 0;
uint32_t clockAnchor = 0;
uint64_t clockAnchorUs = 0;
String logError;
uint32_t corruptRecords = 0;
uint64_t lastAttendanceMs[5] = {};
bool attendanceSeen[5] = {};

uint64_t uptimeMs() { return esp_timer_get_time() / 1000ULL; }
uint32_t currentEpoch() {
  if (!clockSynced) return 0;
  uint64_t epoch = clockAnchor + (esp_timer_get_time() - clockAnchorUs) / 1000000ULL;
  return attendance::validEpoch(epoch) ? uint32_t(epoch) : 0;
}
uint32_t logChecksum(const LogRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(LogRecord, checksum); ++i) hash = attendance::fnvByte(hash, bytes[i]);
  return hash;
}
bool validLog(const LogRecord& record) {
  return record.magic == LOG_MAGIC && record.checksum == logChecksum(record) &&
    record.name[40] == '\0' && record.card[10] == '\0' && record.slot <= 5 &&
    record.kind >= attendance::SCAN && record.kind <= attendance::DENIED &&
    (!record.epoch || attendance::validEpoch(record.epoch));
}
bool blankLogPartition() {
  const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  if (!partition) return false;
  uint8_t buffer[512];
  for (size_t offset = 0; offset < partition->size; offset += sizeof(buffer)) {
    size_t count = min(sizeof(buffer), size_t(partition->size - offset));
    if (esp_partition_read(partition, offset, buffer, count) != ESP_OK) return false;
    for (size_t i = 0; i < count; ++i) if (buffer[i] != 0xFF) return false;
    yield();
  }
  return true;
}
void setupAttendance() {
  bootId = esp_random();
  setenv("TZ", "WIB-7", 1); tzset();
  // Jam selalu dianggap belum sinkron setelah boot; tidak menebak lama padam.
  clockSynced = false;
  logsReady = LittleFS.begin(false);
  // Format hanya partisi benar-benar kosong; tidak menghapus FS lama yang rusak.
  if (!logsReady && blankLogPartition()) {
    logsReady = LittleFS.format() && LittleFS.begin(false);
  }
  if (!logsReady) logError = "Penyimpanan log gagal dibuka; akses pintu tetap berfungsi.";
  Serial.println(logsReady ? "Log LittleFS siap. Sinkronkan jam melalui dashboard." : logError);
}
bool appendLog(uint8_t kind, uint8_t slot, const String& card = "") {
  if (!logsReady) { logError = "Log tidak tersimpan: LittleFS tidak tersedia."; Serial.println(logError); return false; }
  File file = LittleFS.open(LOG_PATH, LittleFS.exists(LOG_PATH) ? "r+" : "w+");
  if (!file) { logError = "Gagal membuka file log."; Serial.println(logError); return false; }
  size_t offset = file.size() / sizeof(LogRecord) * sizeof(LogRecord);
  if (offset + sizeof(LogRecord) > MAX_LOG_BYTES || LittleFS.totalBytes() - LittleFS.usedBytes() < 32768) {
    logError = "Penyimpanan log penuh. Unduh CSV dan lakukan pemeliharaan; log baru tidak tersimpan.";
    Serial.println(logError); return false;
  }
  LogRecord record = {};
  record.magic = LOG_MAGIC; record.epoch = currentEpoch(); record.bootId = bootId;
  record.uptimeMs = uptimeMs(); record.slot = slot; record.kind = kind;
  if (slot >= 1 && slot <= 5) strncpy(record.name, employees[slot - 1].name, 40);
  else strcpy(record.name, "Admin / sistem");
  card.toCharArray(record.card, sizeof(record.card)); record.checksum = logChecksum(record);
  bool ok = file.seek(offset) && file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  file.flush();
  // Baca kembali agar UI tidak melaporkan berhasil hanya berdasarkan panjang write.
  LogRecord verify = {};
  ok = ok && file.seek(offset) && file.read(reinterpret_cast<uint8_t*>(&verify), sizeof(verify)) == sizeof(verify) &&
    validLog(verify) && memcmp(&record, &verify, sizeof(record)) == 0;
  file.close();
  if (!ok) { logError = "Gagal menulis log; periksa penyimpanan."; Serial.println(logError); }
  return ok;
}
String dateTime(uint32_t epoch, const char* format = "%Y-%m-%d %H:%M:%S") {
  if (!epoch) return "Belum sinkron";
  time_t value = epoch; tm local = {}; localtime_r(&value, &local);
  char text[32]; strftime(text, sizeof(text), format, &local); return String(text);
}
String clockStatus() {
  uint32_t now = currentEpoch();
  return now ? "Jam perangkat: " + dateTime(now) + " WIB" : "Jam BELUM SINKRON. Sinkronkan dari HP setelah listrik mati / restart.";
}
String attendancePanel() {
  String body = "<section><h2>Waktu &amp; absensi</h2><p id='clock'>" + escapeHtml(clockStatus()) + "</p>";
  if (!logsReady || !logError.isEmpty()) body += "<p class='notice'>" + escapeHtml(logError) + "</p>";
  body += postForm("/sync") + "<input type='hidden' name='epoch' id='syncEpoch'><button>Sinkronkan jam dari HP / laptop</button></form>"
    "<p>Pastikan jam HP benar. Waktu ditampilkan dalam WIB. Sinkronkan kembali setiap selesai listrik padam.</p>"
    "<p><a href='/attendance'>Lihat rekap 7 hari</a> &nbsp; <a href='/summary.csv'>Unduh rekap 7 hari (CSV)</a></p>"
    "<p><a href='/events.csv'>Unduh seluruh aktivitas (CSV)</a></p>"
    "<p class='muted'>Masuk = scan pertama; keluar sementara = scan terakhir pada tanggal yang sama (00:00–23:59 WIB). "
    "Satu scan belum dianggap keluar. Buka manual tidak dihitung sebagai absensi.</p></section>"
    "<script>document.querySelector(\"form[action='/sync']\").addEventListener('submit',()=>{document.getElementById('syncEpoch').value=Math.floor(Date.now()/1000);});</script>";
  return body;
}
struct WeekData {
  attendance::Summary days[7][5] = {};
  uint32_t unknown = 0, corrupt = 0, outside = 0;
};
void readWeek(WeekData& result, uint32_t now) {
  File file = LittleFS.open(LOG_PATH, "r"); if (!file) return;
  LogRecord record;
  while (file.available() >= int(sizeof(record))) {
    if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record)) break;
    if (!validLog(record)) { ++result.corrupt; continue; }
    if (!record.epoch) { ++result.unknown; continue; }
    if (!attendance::inWeek(record.epoch, now)) { ++result.outside; continue; }
    if (record.slot >= 1 && record.slot <= 5) {
      int day = attendance::localDay(now) - attendance::localDay(record.epoch);
      auto& entry = result.days[day][record.slot - 1];
      entry = attendance::include(entry, record.epoch, record.kind);
    }
    yield();
  }
  if (file.available()) ++result.corrupt;
}
String csvField(String value) {
  // Hindari formula saat CSV dibuka di spreadsheet.
  if (!value.isEmpty() && (value[0] == '=' || value[0] == '+' || value[0] == '-' || value[0] == '@' || value[0] == '\t' || value[0] == '\r')) value = "'" + value;
  value.replace("\"", "\"\""); return "\"" + value + "\"";
}
String kindName(uint8_t kind) {
  switch (kind) {
    case attendance::SCAN: return "AKSES_KARTU";
    case attendance::REPEAT: return "SCAN_ULANG_DIABAIKAN";
    case attendance::MANUAL: return "BUKA_MANUAL";
    case attendance::SYNC: return "SINKRONISASI_JAM";
    default: return "AKSES_DITOLAK";
  }
}
void csvHeaders(const char* filename) {
  noCache(); server.sendHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200, "text/csv; charset=utf-8", "");
  server.sendContent("\xEF\xBB\xBF");
}
void serveWeek(bool csv) {
  if (!requireAdmin()) return; lastAdminMs = millis();
  uint32_t now = currentEpoch();
  if (!now) { server.send(409, "text/plain; charset=utf-8", "Sinkronkan jam melalui dashboard terlebih dahulu. Aktivitas tanpa waktu tersedia di events.csv."); return; }
  if (!logsReady) { server.send(503, "text/plain", "Penyimpanan log tidak tersedia."); return; }
  WeekData week; readWeek(week, now);
  String body;
  if (csv) { csvHeaders("gcoffee-rekap-7-hari.csv"); server.sendContent("Tanggal,ID Karyawan,Nama saat ini,Masuk WIB,Keluar terakhir WIB,Jumlah scan,Status\r\n"); }
  else body = "<p><a href='/'>Kembali ke dashboard</a></p><p>" + escapeHtml(clockStatus()) + "</p>"
    "<p>Scan pertama dan terakhir per tanggal kalender WIB. Hari ini belum final; scan terakhir adalah perkiraan keluar.</p>"
    "<div style='overflow-x:auto'><table border='1' cellpadding='8' style='border-collapse:collapse;background:white'><tr>"
    "<th>Tanggal</th><th>Karyawan</th><th>Masuk</th><th>Keluar terakhir</th><th>Scan</th><th>Status</th></tr>";
  for (int day = 6; day >= 0; --day) for (int slot = 0; slot < 5; ++slot) {
    const auto& entry = week.days[day][slot];
    String date = dateTime(now - day * attendance::DAY, "%Y-%m-%d");
    String first = entry.count ? dateTime(entry.first, "%H:%M:%S") : "-";
    String last = entry.count >= 2 ? dateTime(entry.last, "%H:%M:%S") : "-";
    String status = !entry.count ? "Tidak ada scan bertanggal" : entry.count == 1 ? "Belum ada scan keluar" : "Keluar perkiraan dari scan terakhir";
    if (day == 0 && entry.count) status += " (hari berjalan)";
    String id = "K00" + String(slot + 1);
    if (csv) server.sendContent(csvField(date) + "," + csvField(id) + "," + csvField(employees[slot].name) + "," +
      csvField(first) + "," + csvField(last) + "," + String(entry.count) + "," + csvField(status) + "\r\n");
    else body += "<tr><td>" + date + "</td><td>" + id + " — " + escapeHtml(employees[slot].name) + "</td><td>" + first + "</td><td>" + last + "</td><td>" + String(entry.count) + "</td><td>" + status + "</td></tr>";
  }
  if (csv) {
    server.sendContent("\r\nCatatan,Jumlah\r\nAktivitas tanpa waktu (seluruh log)," + String(week.unknown) +
      "\r\nRecord rusak atau terpotong," + String(week.corrupt) + "\r\n");
    server.sendContent("");
  } else {
    body += "</table></div><p>Aktivitas tanpa waktu (seluruh log): " + String(week.unknown) +
      ". Record rusak/terpotong: " + String(week.corrupt) + ". Data ini tidak dimasukkan ke rekap.</p>"
      "<p>Log tanpa tanggal dapat menyebabkan rekap tidak lengkap; tidak ada scan bukan otomatis tidak hadir.</p>"
      "<p><a href='/summary.csv'>Unduh rekap CSV</a> · <a href='/events.csv'>Unduh seluruh aktivitas CSV</a></p>";
    server.send(200, "text/html; charset=utf-8", page("Rekap 7 hari", body));
  }
}
void serveEvents() {
  if (!requireAdmin()) return; lastAdminMs = millis();
  if (!logsReady) { server.send(503, "text/plain", "Penyimpanan log tidak tersedia."); return; }
  csvHeaders("gcoffee-seluruh-aktivitas.csv");
  server.sendContent("Waktu WIB,Status waktu,Boot ID,Uptime detik,ID Karyawan,Nama saat kejadian,ID kartu,Aktivitas\r\n");
  File file = LittleFS.open(LOG_PATH, "r"); LogRecord record;
  uint32_t corrupt = 0;
  while (file && file.available() >= int(sizeof(record))) {
    if (file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) != sizeof(record)) break;
    if (!validLog(record)) { ++corrupt; continue; }
    char boot[9]; snprintf(boot, sizeof(boot), "%08lx", (unsigned long)record.bootId);
    server.sendContent(csvField(record.epoch ? dateTime(record.epoch) : "") + "," + csvField(record.epoch ? "TERSINKRON" : "BELUM_SINKRON") + "," +
      csvField(boot) + "," + String((unsigned long)(record.uptimeMs / 1000)) + "," + csvField(record.slot ? "K00" + String(record.slot) : "") + "," +
      csvField(record.name) + "," + csvField(record.card) + "," + csvField(kindName(record.kind)) + "\r\n");
    yield();
  }
  if (file && file.available()) ++corrupt;
  if (corrupt) server.sendContent("\r\nRecord rusak atau terpotong dilewati," + String(corrupt) + "\r\n");
  server.sendContent("");
}
void setupAttendanceRoutes() {
  server.on("/sync", HTTP_POST, [] {
    if (!requireAdmin(true)) return;
    String input = server.arg("epoch"); uint64_t epoch = 0; bool valid = input.length() == 10;
    for (unsigned int i = 0; i < input.length() && valid; ++i) {
      if (input[i] < '0' || input[i] > '9') valid = false;
      else epoch = epoch * 10 + (input[i] - '0');
    }
    if (!valid || !attendance::validEpoch(epoch)) { server.send(400, "text/plain", "Jam HP tidak valid. Periksa tanggal perangkat (2024–2037)."); return; }
    timeval tv = {}; tv.tv_sec = epoch;
    if (settimeofday(&tv, nullptr) != 0) { server.send(500, "text/plain", "Gagal menyetel jam."); return; }
    clockAnchor = epoch; clockAnchorUs = esp_timer_get_time(); clockSynced = true;
    bool logged = appendLog(attendance::SYNC, 0);
    notice = "Waktu tersinkron: " + dateTime(currentEpoch()) + " WIB. Log lama tanpa waktu tetap ditandai belum sinkron.";
    if (!logged) notice += " Catatan sinkronisasi gagal disimpan.";
    Serial.println(notice); redirectHome();
  });
  server.on("/attendance", HTTP_GET, [] { serveWeek(false); });
  server.on("/summary.csv", HTTP_GET, [] { serveWeek(true); });
  server.on("/events.csv", HTTP_GET, serveEvents);
}
bool logEmployeeScan(int slot, const String& card) {
  uint64_t now = uptimeMs();
  bool repeat = attendanceSeen[slot] && now - lastAttendanceMs[slot] < 10000;
  bool ok = appendLog(repeat ? attendance::REPEAT : attendance::SCAN, slot + 1, card);
  if (!repeat && ok) { attendanceSeen[slot] = true; lastAttendanceMs[slot] = now; }
  return ok;
}
