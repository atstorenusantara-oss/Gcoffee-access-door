# Uji perangkat setelah upload

Build dapat diperiksa dengan `pio run`. Pengujian berikut membutuhkan ESP32,
reader, kartu, dan browser fisik; jangan dianggap sudah lulus hanya dari build.

1. Boot: GPIO4 dan GPIO15 HIGH. WiFi Gcoffee-Admin belum muncul.
2. Scan kartu awal 4A00A93465: output LOW sekitar 3 detik, lalu HIGH.
   Scan kartu lain / frame checksum salah tidak mengaktifkan output.
3. Tekan tombol singkat: AP tetap mati. Tahan 3 detik: AP aktif.
4. Buka / tanpa login: hanya form PIN; tidak ada nama / ID karyawan.
   POST /open, /employee, /enroll, /commit, /cancel, /pin, /logout, /stop
   tanpa cookie sesi harus ditolak 401. GET /status juga ditolak.
5. Login PIN salah 5 kali: percobaan berikutnya ditolak 60 detik. PIN benar
   memberi akses dashboard. POST dengan cookie valid tetapi token CSRF salah
   harus ditolak 403 dan tidak menulis NVS / membuka relay.
6. Login benar, buka manual: output LOW 3 detik. Ulangi sambil browser mengirim
   permintaan lambat; timer esp_timer harus tetap mengembalikan output HIGH.
7. Daftarkan kartu kedua ke K002: scan tidak membuka pintu selama pendaftaran.
   Konfirmasi simpan, scan ulang di luar pendaftaran: pintu terbuka.
8. Coba daftarkan kartu K002 ke K003: ditolak. Coba Simpan tanpa scan: ditolak.
9. Ganti kartu K001: sebelum konfirmasi kartu lama masih tersimpan; setelah
   konfirmasi, hanya kartu baru diterima. Nama dan ID K001 tetap.
10. Batalkan / tunggu 60 detik saat pendaftaran: data lama tidak berubah.
11. Nonaktifkan K002: scan ditolak. Aktifkan lagi: scan diterima.
12. Ganti PIN, login ulang. Reset / cabut catu, lalu pastikan nama, status,
    kartu pengganti, dan PIN baru tetap berlaku; kartu awal tidak muncul kembali.
13. Logout: cookie lama tidak dapat membuka pintu atau mengubah data.
14. Diamkan dashboard 5 menit: WiFi mati walaupun status dipolling otomatis.
    Akses RFID normal tetap berjalan. Tahan tombol kembali: wajib login ulang.

## Jam dan log absensi

Pemeriksaan `static_assert` di `src/attendance_checks.cpp` dijalankan saat build:
satu/dua scan, jam mundur, scan ulang, buka manual, sinkronisasi, akses ditolak,
catatan tanpa jam, batas tujuh tanggal, tengah malam WIB, dan validasi epoch.
Pengujian perangkat berikut masih perlu dilakukan setelah upload:

15. Boot tanpa sinkronisasi, scan kartu: relay tetap aktif 3 detik. Setelah login,
    CSV seluruh aktivitas memuat BELUM_SINKRON dan uptime; rekap meminta sinkronisasi.
16. POST /sync tanpa login ditolak 401; token CSRF salah ditolak 403. Timestamp
    kosong, nonangka, negatif, di luar 2024–2037, atau terlalu panjang ditolak 400.
    GET /attendance, /summary.csv, /events.csv tanpa login ditolak 401.
17. Sinkronkan dari HP yang jamnya benar: dashboard menampilkan WIB, termasuk
    jika HP menggunakan zona waktu berbeda. Unduh CSV dan cek catatan sinkronisasi.
18. Scan satu kartu: rekap tanggal itu mempunyai masuk dan belum ada keluar.
    Scan lagi dalam 10 detik: log scan ulang bertambah, jumlah rekap tidak bertambah.
    Scan setelah 10 detik: jam terakhir menjadi perkiraan keluar.
19. Buka pintu manual: hanya aktivitas BUKA_MANUAL, bukan scan absensi karyawan.
20. Nonaktif / kartu asing / pendaftaran: tidak menambah absensi. Ganti kartu
    karyawan dan scan lagi: tetap tergabung pada K001–K005 yang sama.
21. Uji tanggal lintas bulan/tahun dengan sinkronisasi; rekap berisi tepat hari
    ini dan 6 tanggal sebelumnya. Batas 00:00 WIB memisahkan tanggal.
22. Reset/cabut daya: semua log lama tetap ada, jam meminta sinkronisasi lagi.
    Scan sebelum sinkronisasi menjadi tanpa waktu, tidak salah diberi tanggal lama.
    Sinkronkan kembali: aktivitas tanpa waktu tidak diubah menjadi tanggal palsu.
23. CSV berisi nama berkarakter kutip/koma/& tanpa merusak kolom; nama yang
    diawali =,+,-,@ dilindungi agar tidak menjadi formula. HTML nama harus di-escape.
24. Unduh CSV dua kali / ganti jam: log tidak terhapus. Nama lama pada CSV aktivitas
    tetap nama saat kejadian, sedangkan rekap menampilkan nama terkini.
25. Uji di perangkat cadangan: padam saat menulis record, record terpotong dilewati
    dan scan berikutnya tetap bisa dicatat. Jangan dianggap semua kejadian tepat
    sebelum listrik padam pasti tersimpan.
26. Simulasikan storage penuh/gagal di perangkat uji: kesalahan terlihat pada
    serial dan dashboard; akses RFID dan timer relay tetap bekerja. Filesystem
    yang tidak kosong namun rusak tidak boleh otomatis diformat.

## Buzzer GPIO22 aktif LOW

27. Boot: GPIO22 HIGH, buzzer diam. Kartu aktif: satu beep sekitar 150 ms dan
    relay/LED tetap aktif 3 detik. Kartu asing/nonaktif: satu beep sekitar 800 ms,
    tanpa membuka relay atau menambah absensi.
28. Saat pendaftaran / checksum salah / buka manual: tidak ada beep kartu.
    Uji sambil dashboard aktif: buzzer kembali HIGH tanpa menunggu request selesai.
29. Scan berikutnya ketika beep masih aktif mengulang timer dengan durasi hasil
    scan terakhir; buzzer tidak tertinggal LOW.

## Upload firmware lewat dashboard

Pemeriksaan otomatis: `pio run` dan `node tests/firmware_hash.test.cjs`.
Tes Node membandingkan SHA256 browser dengan Node crypto untuk vektor dikenal,
data biner, batas padding, ukuran mendekati slot OTA dan firmware hasil build.
Pengujian berikut memerlukan perangkat cadangan dan belum digantikan oleh build:

30. Pasang firmware OTA lewat USB sekali; login lalu pastikan panel upload tampil.
    POST /firmware tanpa sesi ditolak 401; cookie valid tetapi header
    X-CSRF-Token salah/kosong ditolak 403 sebelum menulis flash.
31. Pilih firmware.bin hasil build: progress berjalan, pesan berhasil muncul,
    perangkat restart, AP mati. Aktifkan AP lagi, login dan sinkronkan jam.
    Nama, kartu, PIN dan CSV aktivitas lama tetap ada. Ulangi OTA ke slot lainnya.
32. Coba source, bootloader, merged binary, image chip lain, file kosong, file
    lebih besar dari slot, header ukuran salah, dan checksum SHA256 salah.
    Semua harus ditolak tanpa restart atau mengganti boot partition.
33. Putus koneksi saat transfer dan coba request multipart berisi dua file.
    Firmware lama tetap berjalan; setelah transfer dibatalkan/timeout bisa
    mencoba upload lagi. Pada perangkat uji, padam saat menulis slot OTA lalu
    boot ulang: firmware lama harus tetap dapat berjalan.
34. Mulai upload saat relay/buzzer aktif: keduanya kembali HIGH, kartu tidak
    membuka pintu selama transfer. Setelah gagal, akses kartu kembali bekerja.
35. Unduh log sebelum dan sesudah OTA: data lama tetap sama. Jam meminta
    sinkronisasi ulang. Firmware pengganti yang memiliki bug runtime mungkin
    membutuhkan pemulihan USB; tidak ada rollback otomatis pada versi ini.
