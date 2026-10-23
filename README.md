# Absensi Gcoffee — uji pembaca RFID

Proyek PlatformIO untuk ESP32 Dev Module (ESP32 klasik) dan reader ID-12LA.
Program membaca ID kartu beserta checksum dan menampilkannya melalui serial.
Kartu terdaftar dan aktif dengan checksum valid menyalakan relay GPIO4 dan LED
tombol GPIO15 (keduanya aktif LOW) selama 3 detik. Scan valid kartu terdaftar
berikutnya mengulang durasi 3 detik. Saat boot keduanya diatur mati (HIGH).
Tombol GPIO23 mengaktifkan mode admin jika ditahan 3 detik.
GPIO22 menjadi output buzzer aktif LOW: kartu terdaftar dan aktif berbunyi
sekali 150 ms; kartu tidak terdaftar/nonaktif berbunyi sekali 800 ms.
Buzzer mati (HIGH) saat boot. Timer berjalan tanpa delay sehingga pembacaan,
dashboard, dan timer relay tetap bekerja. Scan berikutnya memulai ulang durasi beep.
Scan pendaftaran, checksum salah, dan buka manual tidak membunyikan buzzer.
Log akses kartu dan buka manual disimpan permanen di LittleFS.
Kartu lain ditampilkan sebagai akses ditolak, tanpa mengubah output atau timer.
Data lima karyawan dan PIN disimpan di NVS menggunakan Preferences.
Saat pertama dipakai, kartu `4A00A93465` terdaftar untuk K001. Setelah diganti
atau dinonaktifkan, restart tidak mengembalikan kartu awal tersebut.

## Dashboard admin dari HP / laptop

1. Pasang tombol antara **GPIO23 dan GND** (`INPUT_PULLUP`, tanpa catu eksternal).
2. Setelah upload, tahan tombol 3 detik. WiFi admin aktif; serial menampilkan IP.
3. Hubungkan HP ke WiFi **Gcoffee-Admin**, password sesuai `AP_PASSWORD` di `src/config.h`.
   Pilih tetap terhubung jika HP memberi tahu jaringan tidak memiliki internet.
4. Buka **http://192.168.4.1** di browser (HTTP, bukan HTTPS).
5. Masukkan PIN awal sesuai `INITIAL_PIN` di `src/config.h`. Ganti PIN melalui dashboard setelah login.

WiFi hanya aktif setelah tombol ditekan dan mati setelah 5 menit tanpa aktivitas
admin. Pembaruan status otomatis tidak memperpanjang waktu ini. Satu perangkat
dapat terhubung ke AP pada satu waktu. Login salah 5 kali dibatasi selama 60 detik.
Logout mengakhiri sesi dan pendaftaran; tombol Matikan mode admin mematikan WiFi.
PIN baru tersimpan di NVS; password WiFi dapat diubah pada `src/config.h` lalu upload.

## Sinkronisasi waktu tanpa RTC

Setelah **setiap boot / restart / listrik padam**, buka dashboard dan tekan
**Sinkronkan jam dari HP / laptop**. Jam HP harus benar. Browser mengirim waktu
perangkat saat tombol ditekan; ESP32 menampilkan waktu **WIB (UTC+7)**,
tanpa memerlukan koneksi internet atau hardware tambahan.

Jam tetap berjalan meskipun WiFi admin dimatikan, selama ESP32 tetap menyala.
Sistem sengaja tidak menggunakan jam sebelum sinkronisasi setelah boot karena
tidak bisa mengetahui berapa lama listrik padam. Versi core saat ini menerima
tanggal 2024–2037. Menyinkronkan kembali tidak mengubah timestamp log sebelumnya.

**Sebelum disinkronkan, kartu terdaftar tetap membuka pintu**, dan aktivitas
disimpan dengan status **BELUM_SINKRON**, ID boot, serta waktu sejak boot.
Tanggal/jam kejadian tersebut tidak ditebak atau diperbaiki otomatis kemudian.
Catatan ini bisa diunduh, tetapi tidak dimasukkan ke rekap tanggal tertentu.
Jika Anda baru mengecek mingguan setelah listrik sempat mati, aktivitas antara
padam dan sinkronisasi tidak punya tanggal yang dapat dipercaya. Sinkronkan
sesegera mungkin setelah daya kembali agar absensi bertanggal tetap lengkap.

## Rekap mingguan dan CSV

- **Lihat rekap 7 hari**: hari ini + 6 tanggal sebelumnya, berdasarkan WIB.
- **Unduh rekap 7 hari (CSV)**: tabel 5 karyawan per hari.
- **Unduh seluruh aktivitas (CSV)**: setiap scan diterima, scan ulang, buka
  manual, dan sinkronisasi jam yang berhasil dicatat, termasuk yang tanpa waktu.

Aturan rekap menggunakan tanggal kalender 00:00–23:59 WIB, bukan pergantian shift:
scan pertama sebagai masuk, scan terakhir sebagai **perkiraan keluar**. Hanya satu
scan diberi status **Belum ada scan keluar**. Hari ini ditandai masih berjalan.
Keluar-masuk pintu di tengah hari tetap tersimpan sebagai aktivitas akses.
Shift melewati tengah malam akan terpisah dua tanggal pada versi ini.

Scan karyawan yang sama dalam 10 detik sejak scan absensi terakhir dicatat sebagai
scan ulang dan tidak menambah hitungan rekap; akses pintu tetap dapat dibuka.
Buka manual tidak dihitung sebagai absensi. Kartu ditolak dan scan pendaftaran
tidak dimasukkan ke log absensi. Data tanpa waktu / record rusak tidak dimasukkan
ke rekap, sehingga baris tanpa scan **bukan otomatis karyawan tidak hadir**.

Log merujuk ID karyawan tetap K001–K005. Ganti kartu tidak memutus rekapnya.
CSV aktivitas menyimpan nama dan ID kartu saat kejadian; rekap menampilkan nama
karyawan saat ini. Hindari memakai ulang slot karyawan lama untuk orang berbeda
tanpa mengarsipkan lognya.

### Ketahanan dan kapasitas log

Data disimpan setelah setiap kejadian, dengan checksum dan pembacaan ulang untuk
memeriksa hasil tulis. Record terakhir yang terpotong akibat listrik padam tidak
dianggap valid; aktivitas yang sedang ditulis saat padam mungkin tidak tersimpan.
Tidak ada penghapusan otomatis ketika log dibaca, diunduh, atau jam disinkronkan.

Batas file log 1 MiB, yaitu maksimal **13.107 kejadian** berukuran 80 byte.
Untuk 5 karyawan × 2 scan × 30 hari, dibutuhkan sekitar 24 KB; scan tambahan,
buka manual, dan sinkronisasi juga memakai kapasitas. Unduh CSV secara berkala.
Jika penuh atau filesystem gagal, dashboard/serial menampilkan kesalahan dan
akses pintu tetap bekerja; log baru tidak dijamin tersimpan. Perbaikan/pengosongan
penyimpanan belum tersedia lewat dashboard dan tidak dilakukan otomatis.

LittleFS hanya diformat otomatis saat partisinya benar-benar kosong. Jika partisi
berisi data tetapi tidak bisa dibuka, sistem melaporkan gagal tanpa menghapusnya.
Gunakan upload firmware biasa; **jangan upload filesystem (`uploadfs`) atau erase
flash** jika ingin mempertahankan log. Partisi default tetap dipakai, NVS tidak dipindah.

Dashboard menyediakan nama/status lima karyawan, pendaftaran/ganti kartu,
penggantian PIN, dan tombol buka pintu 3 detik. Semua tindakan memerlukan login
dan token formulir. Tombol fisik tidak langsung membuka pintu.

### Daftarkan atau ganti kartu

1. Pilih **Daftarkan kartu** atau **Ganti kartu** pada karyawan yang sesuai.
2. Dalam 60 detik, tempelkan kartu. Kandidat pertama ditahan untuk konfirmasi;
   lihat pesan status di bagian atas dashboard.
3. Tekan **Simpan kartu yang terbaca**. Kartu baru aktif setelah NVS berhasil
   ditulis. Kartu lama tidak lagi berlaku. Kartu yang dimiliki karyawan lain ditolak,
   termasuk jika kartu karyawan tersebut sedang nonaktif.
4. Pilih **Batalkan pendaftaran** untuk membatalkan tanpa mengubah kartu lama.

Selama mode pendaftaran, scan tidak membuka relay. Nama dan ID karyawan K001–K005
tetap sama saat kartunya diganti. Untuk mencabut akses kartu hilang, hilangkan
centang Akses kartu aktif lalu Simpan nama / status. Untuk mengaktifkan lagi,
centang dan simpan. Pendaftaran kartu baru otomatis mengaktifkan aksesnya.

Penyimpanan memakai satu record NVS per karyawan; RAM diperbarui hanya setelah
penulisan berhasil. Jangan menjalankan erase flash jika ingin mempertahankan data.
Upload biasa dengan partisi tetap mempertahankan NVS. Versi Arduino IDE di
`rfid_sederhana/` merupakan arsip pembaca sederhana, bukan firmware dashboard.

## Upload dari folder ini

PlatformIO Core dan ekstensi PlatformIO IDE sudah terpasang pada komputer ini.
Buka folder ini di VS Code, lalu gunakan tombol **Build**, **Upload**, dan
**Serial Monitor** pada toolbar PlatformIO atau melalui Project Tasks.
Tutup Serial Monitor sebelum upload jika port sedang dipakai.

Task VS Code juga tersedia melalui **Terminal > Run Task**:
`RFID: Build`, `RFID: Upload`, dan `RFID: Serial Monitor`.
Task ini menggunakan PlatformIO Core milik ekstensi secara langsung agar
tidak tertukar dengan instalasi `pio` lain pada PATH. Ctrl+Shift+B menjalankan build.

Alternatif melalui terminal di folder ini:

```powershell
pio run
pio run --target upload
pio device monitor
```

Tekan Ctrl+C untuk menutup monitor. Serial Monitor memakai 115200 baud;
komunikasi ESP32 dengan reader memakai 9600 baud.

Konfigurasi di `platformio.ini` memakai `esp32dev`, platform Espressif32 6.13.0,
serta COM10. COM10 terdeteksi sebagai USB-SERIAL CH340 saat setup; nama USB
tersebut belum membuktikan model chip ESP32. Konfigurasi mengikuti ESP32 klasik
pada program referensi. Jika memakai board berbeda, sesuaikan `board`.
Jika port berubah, lihat `pio device list`, lalu ubah `upload_port` dan
`monitor_port` pada `platformio.ini`.

## Wiring ID-12LA

| Pin modul ID-12LA | Sambungan |
| --- | --- |
| 1 — GND | GND ESP32 |
| 2 — RES | 3V3 / pin 11 ID-12LA |
| 7 — Format | GND |
| 9 — D0 | GPIO16 ESP32 |
| 11 — VCC | 3V3 ESP32 |
| 3, 4, 5, 6, 8, 10 | Tidak disambungkan |

Output tambahan ESP32: GPIO4 ke IN modul relay yang kompatibel logika 3,3 V,
GPIO15 ke rangkaian LED tombol aktif LOW. Gunakan resistor pembatas untuk LED
eksternal dan catu relay sesuai spesifikasi modul; jangan hubungkan kumparan
relay langsung ke GPIO. Angka pin pada tabel di atas adalah pin reader RFID.

Buzzer: **GPIO22 ke IN modul buzzer aktif LOW** yang kompatibel dengan logika
3,3 V, GND modul ke GND ESP32, dan VCC mengikuti spesifikasi modul.
Program memakai buzzer aktif (berbunyi saat diaktifkan), bukan piezo pasif yang
memerlukan sinyal nada. Gunakan modul/driver yang sesuai; jangan membebani GPIO
dengan buzzer berarus besar atau memasukkan sinyal 5 V ke GPIO.

Gunakan kartu 125 kHz EM4001 atau kompatibel. Nomor di atas adalah kaki modul,
bukan urutan header breakout. Untuk breakout dengan catu 5 V, gunakan pengubah
level ke 3,3 V sebelum sinyal masuk GPIO ESP32. GPIO16 harus tersedia pada board.

## File

- `src/main.cpp`: sumber utama yang di-build PlatformIO.
- `src/admin.h`: dashboard, login, data karyawan NVS, dan timer relay.
- `src/attendance.h`: sinkronisasi jam, penyimpanan aktivitas, rekap, dan CSV.
- `src/attendance_logic.h`: aturan rekap dan batas waktu.
- `src/attendance_checks.cpp`: pemeriksaan aturan rekap saat kompilasi.
- `platformio.ini`: konfigurasi board, upload, dan serial monitor.
- `rfid_sederhana/`: versi Arduino IDE dan panduan tambahan.
- `preseferensi.md`: program referensi awal.

Contoh satu pembacaan valid:

```text
TAG: 010203040501 | ID: 0102030405
```

`TAG` berisi 10 karakter ID dan 2 karakter checksum. Jauhkan kartu sebelum
menempelkan lagi. Lihat aturan scan ulang 10 detik untuk penghitungan rekap.

## Konfigurasi lokal sebelum build

Salin `src/config.example.h` menjadi `src/config.h`, lalu isi password WiFi
(minimal 8 karakter) dan PIN awal (6?12 angka). File `config.h` diabaikan Git.
PIN yang sudah tersimpan di NVS tetap berlaku sampai diubah melalui dashboard.
