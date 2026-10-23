# Pembaca RFID sederhana untuk Absensi Gcoffee

Diambil dari `../preseferensi.md`: ESP32 klasik dengan ID-12LA, UART2 RX
GPIO16, 9600 baud, 8N1. Program ini hanya membaca kartu, memeriksa checksum,
dan menampilkan ID di Serial Monitor. WiFi, ESP-NOW, tombol, relay, dan LED
tambahan tidak digunakan. File referensi tetap utuh.

## Sambungan pin

Matikan catu daya sebelum memasang kabel. Untuk modul ID-12LA tanpa rangkaian
adapter tambahan, gunakan catu 3,3 V agar level sinyal sesuai dengan ESP32.

| Pin ID-12LA | Sambungkan ke | Fungsi |
| --- | --- | --- |
| 1 (GND) | GND ESP32 | Ground bersama |
| 2 (RES) | Pin 11 ID-12LA / 3V3 | Reset ditahan HIGH |
| 7 (Format) | GND | Memilih keluaran ASCII saat dinyalakan |
| 9 (D0) | GPIO16 ESP32 | Data serial menuju RX UART2 |
| 11 (VCC) | 3V3 ESP32 | Catu modul ID-12LA |
| 3, 4, 5, 6, 8, 10 | Tidak disambungkan | Tidak diperlukan pada versi ini |

Hanya **GPIO16** yang digunakan oleh sketch. GPIO17 (Tag in Range), GPIO2,
GPIO23, GPIO15, GPIO4, dan GPIO22 dari program lama tidak digunakan.
Tidak perlu kabel TX ESP32 ke reader karena komunikasinya satu arah.

Nomor di tabel adalah nomor kaki modul ID-12LA, bukan urutan header pada
breakout. Cocokkan dengan penandaan dan gambar pinout datasheet. Jika memakai
breakout dengan regulator atau catu 5 V, ikuti spesifikasi breakout dan gunakan
pengubah level 5 V ke 3,3 V pada data sebelum GPIO16; jangan masukkan sinyal
5 V langsung ke GPIO ESP32. Pastikan GPIO16 tersedia pada board yang dipakai;
beberapa board ESP32 dengan PSRAM memakai pin tersebut secara internal.

## Upload dan mencoba

1. Buka `rfid_sederhana.ino` di Arduino IDE.
2. Jika belum ada, pasang paket board **esp32 by Espressif Systems** melalui
   Boards Manager. Tidak perlu library RFID tambahan.
3. Pilih board ESP32 yang sesuai (misalnya ESP32 Dev Module untuk board
   ESP32 klasik yang kompatibel), lalu pilih port USB.
4. Upload, buka Serial Monitor pada **115200 baud**, lalu tekan EN/Reset
   jika ingin melihat pesan awal.
5. Tempelkan kartu **125 kHz EM4001 atau kompatibel**. Jauhkan kartu sebelum
   menempelkan ulang. Kartu 13,56 MHz seperti MIFARE tidak sesuai reader ini.

Contoh keluaran untuk satu frame valid:

```text
TAG: 010203040501 | ID: 0102030405
```

`ID` adalah 10 karakter hex data kartu. `TAG` memuat data tersebut beserta
2 karakter checksum, mempertahankan format 12 karakter program sebelumnya.
Angka nol di awal dipertahankan. Data baru ditampilkan setelah frame lengkap
dan checksum XOR valid diterima. Ini menampilkan setiap frame valid dari reader;
belum ada aturan pencegah absensi ganda.

## Tahap absensi berikutnya

Versi ini belum menyimpan log permanen atau tanggal/jam. Serial Monitor hanya
menampilkan hasil pembacaan. Untuk implementasi kedai kopi, langkah berikutnya
adalah mendaftarkan ID kartu ke nama karyawan, menentukan aturan masuk/pulang
dan pencegahan scan ganda, lalu menyimpan log berisi ID, karyawan, tanggal/jam,
dan jenis absensi. Sumber waktu dan tempat penyimpanan dipilih pada tahap itu.

## Referensi dan status verifikasi

- Program awal: `../preseferensi.md`.
- [Datasheet ID Innovations ID-3/12/20LA](https://cdn.sparkfun.com/assets/9/3/0/5/2/DS-11827-RFID_Reader_ID-12LA__125_kHz_.pdf),
  khususnya bagian 4, 6, dan 10.1: frame ASCII, pin format, dan pin 9 ke UART RX.
- [Arduino-ESP32 UART API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html).

Belum diuji pada perangkat fisik atau dikompilasi dengan toolchain ESP32 di
lingkungan ini. Lakukan upload dan uji scan kartu untuk verifikasi perangkat.
