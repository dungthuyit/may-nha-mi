# may-nha-mi

Mây nhà Mi — firmware ESP32 cho trạm thời tiết

SHT4x, BMP280 và màn hình ST7735S 1.8 inch.

Repo này chỉ chứa mã ESP32 và web chạy trên ESP32. Mã Android và máy chủ cloud không công khai. APK Android được phát hành riêng trong Releases.

## Build bằng PlatformIO

1. Cài PlatformIO và mở thư mục này.
2. Sao chép `include/secrets.example.h` thành `include/secrets.h`, điền Wi-Fi.
3. Sao chép `include/maintenance_secret.example.h` thành `include/maintenance_secret.h`, đặt mật khẩu riêng. Tài khoản trang `/setup` là `mi`.
4. Tùy chọn: cấu hình cloud trong `include/cloud_secrets.h` theo file mẫu. Để trống nếu không sử dụng cloud.
5. Thay tọa độ dự báo mẫu trong `src/main.cpp` bằng khu vực của bạn. Hiệu chỉnh nhiệt độ mặc định của bản công khai là 0.
6. Chạy `pio run`. File OTA là `.pio/build/esp32dev/firmware.bin`; lần đầu dùng `pio run -t upload` qua USB.

Không công khai các file cấu hình riêng hoặc firmware `.bin` đã chứa mật khẩu. Tự đặt mật khẩu trước khi cấp nguồn. Cấu hình hiện có trong NVS có thể ghi đè các giá trị mặc định sau cập nhật.

## Chân nối

- SHT4x và BMP280: SDA21, SCL22, 3V3, GND. BMP280: CSB lên 3V3, SDO xuống GND.
- ST7735S: SCL18, SDA23, CS27, DC25, RST26, BLK32; nguồn 3V3.
- Bỏ cầu nối VDD–BLK trước khi điều khiển đèn nền bằng GPIO32.

Dự báo khu vực: [Open-Meteo](https://open-meteo.com/), CC BY 4.0. Ước lượng áp suất chỉ mang tính tham khảo.

## Cập nhật Android

Tải APK từ Releases, cài đè bản cũ. Từ bản 1.14, mở Thiết bị → Cập nhật ứng dụng. Android vẫn yêu cầu xác nhận cài đặt. Không cần cấp key trạm cho GitHub.
