// ESP32 klasik + ID-12LA: baca kartu dan tampilkan di Serial Monitor.
// ID-12LA pin 9 (D0) -> GPIO16. Lihat README.md untuk wiring lengkap.
#include <Arduino.h>

constexpr int RFID_RX_PIN = 16;
HardwareSerial RFIDSerial(2);

char frame[13];  // 10 karakter ID + 2 karakter checksum + terminator.
uint8_t frameLength = 0;
bool receiving = false;
unsigned long lastByteMs = 0;

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

void printCard() {
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < 10; i += 2) {
    checksum ^= (hexValue(frame[i]) << 4) | hexValue(frame[i + 1]);
  }
  const uint8_t receivedChecksum =
      (hexValue(frame[10]) << 4) | hexValue(frame[11]);
  if (checksum != receivedChecksum) {
    Serial.println("RFID ditolak: checksum salah.");
    return;
  }

  frame[12] = '\0';
  Serial.print("TAG: ");
  Serial.print(frame);  // Format 12 hex yang digunakan program referensi.
  Serial.print(" | ID: ");
  Serial.write(reinterpret_cast<const uint8_t*>(frame), 10);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  RFIDSerial.begin(9600, SERIAL_8N1, RFID_RX_PIN, -1);
  Serial.println("RFID siap. Tempelkan kartu, lalu jauhkan sebelum scan ulang.");
}

void loop() {
  // Buang frame terpotong agar tidak tercampur dengan pembacaan berikutnya.
  if (receiving && millis() - lastByteMs > 100) {
    receiving = false;
    frameLength = 0;
  }

  while (RFIDSerial.available() > 0) {
    const char c = static_cast<char>(RFIDSerial.read());
    lastByteMs = millis();

    if (c == 0x02) {  // STX: awal frame ID-12LA.
      receiving = true;
      frameLength = 0;
      continue;
    }
    if (!receiving) continue;

    if (c == 0x03) {  // ETX: akhir frame.
      if (frameLength == 12) printCard();
      receiving = false;
      frameLength = 0;
    } else if (c == '\r' || c == '\n') {
      // CR/LF adalah bagian penutup frame, sebelum ETX.
      continue;
    } else if (hexValue(c) >= 0 && frameLength < 12) {
      frame[frameLength++] = (c >= 'a' && c <= 'f') ? c - 'a' + 'A' : c;
    } else {
      receiving = false;  // Karakter tidak valid atau frame terlalu panjang.
      frameLength = 0;
    }
  }
}
