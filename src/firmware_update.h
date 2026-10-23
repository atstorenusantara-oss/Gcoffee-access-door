#pragma once
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_image_format.h>
#include <mbedtls/sha256.h>
#include "firmware_hash_script.h"

bool firmwareReceiving = false, firmwareSeen = false, firmwareComplete = false;
bool firmwareReboot = false;
size_t firmwareExpected = 0, firmwareBytes = 0;
unsigned long firmwareLastMs = 0, firmwareRebootMs = 0;
int firmwareStatus = 400;
String firmwareError;
String firmwareExpectedHash;
mbedtls_sha256_context firmwareHash;
bool firmwareHashActive = false;

void failFirmware(int status, const String& message) {
  if (Update.isRunning()) Update.abort();
  if (firmwareHashActive) { mbedtls_sha256_free(&firmwareHash); firmwareHashActive = false; }
  firmwareReceiving = false; firmwareComplete = false;
  firmwareStatus = status; firmwareError = message;
  notice = "Upload firmware gagal: " + message;
}
bool firmwareAuthorized() {
  if (!sessionValid()) { failFirmware(401, "Sesi berakhir. Login kembali."); return false; }
  // Header tersedia sebelum parser multipart memanggil callback upload.
  if (csrfToken.isEmpty() || server.header("X-CSRF-Token") != csrfToken) {
    failFirmware(403, "Token upload tidak valid."); return false;
  }
  return true;
}
String firmwarePanel() {
  return "<section><h2>Upload program / firmware</h2>"
    "<p>Pilih firmware.bin hasil build untuk perangkat ini. Data karyawan, PIN dan log tetap tersimpan jika layout partisi sama. "
    "Pintu tidak melayani scan selama transfer. Jangan matikan daya; perangkat restart setelah berhasil.</p>"
    "<form id='fw-form'><label>Firmware aplikasi (.bin)</label><input id='fw-file' type='file' accept='.bin' required>"
    "<br><button id='fw-button'>Upload firmware</button></form><p id='fw-result' role='status'></p>"
    "<progress id='fw-progress' max='100' value='0' hidden></progress>"
    "<script>" + String(FPSTR(FIRMWARE_HASH_SCRIPT)) + "document.getElementById('fw-form').onsubmit=async e=>{e.preventDefault();"
    "const f=document.getElementById('fw-file').files[0],b=document.getElementById('fw-button'),"
    "r=document.getElementById('fw-result'),p=document.getElementById('fw-progress');"
    "if(!f||!f.size||!f.name.toLowerCase().endsWith('.bin')){r.textContent='Pilih file .bin yang tidak kosong.';return;}"
    "if(f.size>1310720){r.textContent='Firmware melebihi kapasitas slot OTA (1.310.720 byte).';return;}"
    "if(!confirm('Upload firmware dan restart perangkat?'))return;"
    "b.disabled=true;p.hidden=false;p.value=0;r.textContent='Mengirim firmware...';"
    "let hash;try{r.textContent='Memeriksa checksum...';hash=await firmwareSha256(f);}"
    "catch(e){r.textContent='File tidak dapat dibaca.';b.disabled=false;return;}"
    "const x=new XMLHttpRequest();x.open('POST','/firmware');x.timeout=180000;"
    "x.setRequestHeader('X-Firmware-SHA256',hash);"
    "x.setRequestHeader('X-CSRF-Token','" + csrfToken + "');x.setRequestHeader('X-Firmware-Size',String(f.size));"
    "x.upload.onprogress=e=>{if(e.lengthComputable)p.value=Math.round(e.loaded/e.total*100);};"
    "x.onload=()=>{r.textContent=x.responseText;b.disabled=false;if(x.status===200){p.value=100;"
    "r.textContent+=' Sambungkan kembali WiFi admin, login, lalu sinkronkan jam.';}};"
    "x.onerror=x.ontimeout=()=>{r.textContent='Koneksi terputus. Periksa perangkat lalu login kembali sebelum mencoba ulang.';b.disabled=false;};"
    "const d=new FormData();d.append('firmware',f);x.send(d);};</script></section>";
}
void receiveFirmware() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (firmwareSeen || firmwareReboot) { failFirmware(400, "Kirim tepat satu file firmware."); return; }
    firmwareSeen = true; firmwareError = ""; firmwareComplete = false;
    firmwareLastMs = millis();
    firmwareBytes = 0; firmwareExpected = 0;
    if (!firmwareAuthorized()) return;
    firmwareExpectedHash = server.header("X-Firmware-SHA256");
    if (firmwareExpectedHash.length() != 64) { failFirmware(400, "Checksum SHA256 diperlukan."); return; }
    for (unsigned int i = 0; i < 64; ++i) {
      char c = firmwareExpectedHash[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { failFirmware(400, "Checksum tidak valid."); return; }
    }
    String size = server.header("X-Firmware-Size");
    if (size.isEmpty() || size.length() > 8) { failFirmware(400, "Ukuran firmware tidak valid."); return; }
    for (unsigned int i = 0; i < size.length(); ++i) {
      if (size[i] < '0' || size[i] > '9') { failFirmware(400, "Ukuran firmware tidak valid."); return; }
    }
    firmwareExpected = strtoul(size.c_str(), nullptr, 10);
    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    String filename = upload.filename; filename.toLowerCase();
    if (upload.name != "firmware" || !filename.endsWith(".bin") || firmwareExpected < 288) {
      failFirmware(400, "Pilih firmware aplikasi .bin, bukan source atau filesystem."); return;
    }
    if (!partition || firmwareExpected > partition->size) { failFirmware(413, "Firmware melebihi kapasitas partisi OTA."); return; }
    cancelEnrollment();
    if (doorTimer) esp_timer_stop(doorTimer);
    if (buzzerTimer) esp_timer_stop(buzzerTimer);
    closeDoor(nullptr); stopBuzzer(nullptr);
    if (!Update.begin(firmwareExpected, U_FLASH)) { failFirmware(500, Update.errorString()); return; }
    mbedtls_sha256_init(&firmwareHash); firmwareHashActive = true;
    if (mbedtls_sha256_starts_ret(&firmwareHash, 0) != 0) { failFirmware(500, "Gagal memulai checksum."); return; }
    firmwareReceiving = true; firmwareLastMs = lastAdminMs = millis();
    notice = "Upload firmware berlangsung; scan kartu sementara dihentikan.";
  } else if (upload.status == UPLOAD_FILE_WRITE && firmwareReceiving) {
    if (!firmwareAuthorized()) return;
    firmwareLastMs = lastAdminMs = millis();
    if (upload.currentSize > firmwareExpected - firmwareBytes) { failFirmware(400, "Ukuran file tidak sesuai."); return; }
    if (firmwareBytes == 0) {
      // Aplikasi ESP32 memiliki image header, segment header, lalu app descriptor.
      esp_image_header_t header = {};
      uint32_t appMagic = 0;
      if (upload.currentSize < sizeof(header) + 12) { failFirmware(400, "Header firmware terpotong."); return; }
      memcpy(&header, upload.buf, sizeof(header));
      memcpy(&appMagic, upload.buf + sizeof(header) + 8, sizeof(appMagic));
      if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != ESP_CHIP_ID_ESP32 || appMagic != ESP_APP_DESC_MAGIC_WORD) {
        failFirmware(400, "File firmware tidak sesuai dengan perangkat ini."); return;
      }
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { failFirmware(500, Update.errorString()); return; }
    if (mbedtls_sha256_update_ret(&firmwareHash, upload.buf, upload.currentSize) != 0) { failFirmware(500, "Gagal menghitung checksum."); return; }
    firmwareBytes += upload.currentSize;
  } else if (upload.status == UPLOAD_FILE_END && firmwareReceiving) {
    if (firmwareBytes != firmwareExpected || upload.totalSize != firmwareExpected) {
      failFirmware(400, "Upload tidak lengkap atau ukuran file berbeda."); return;
    }
    uint8_t digest[32]; char hex[65];
    if (mbedtls_sha256_finish_ret(&firmwareHash, digest) != 0) { failFirmware(500, "Gagal memeriksa checksum."); return; }
    mbedtls_sha256_free(&firmwareHash); firmwareHashActive = false;
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (firmwareExpectedHash != hex) { failFirmware(400, "Checksum berbeda. Upload ditolak."); return; }
    firmwareComplete = true;
    // Jangan aktifkan image sebelum seluruh request selesai (termasuk penolakan file kedua).
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    failFirmware(400, "Transfer dibatalkan. Firmware lama tetap digunakan."); firmwareSeen = false;
  }
}
void finishFirmware() {
  noCache();
  if (!firmwareAuthorized()) { /* Pesan disiapkan oleh pemeriksaan akses. */ }
  else if (!firmwareComplete || !firmwareReceiving) {
    if (firmwareError.isEmpty()) failFirmware(400, "Tidak ada firmware lengkap yang diterima.");
  } else if (!Update.end()) failFirmware(500, Update.errorString());
  else {
    firmwareReceiving = false; firmwareComplete = false;
    firmwareReboot = true; firmwareRebootMs = millis();
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", "Firmware berhasil disimpan. Perangkat akan restart.");
    return;
  }
  server.send(firmwareStatus, "text/plain; charset=utf-8", firmwareError);
  firmwareSeen = false;
}
void pollFirmware() {
  if (firmwareSeen && !firmwareReboot && millis() - firmwareLastMs >= 30000) {
    failFirmware(408, "Transfer berhenti lebih dari 30 detik."); firmwareSeen = false;
  }
  if (firmwareReboot && millis() - firmwareRebootMs >= 1200) ESP.restart();
}
