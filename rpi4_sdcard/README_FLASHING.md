# Инструкция по развертыванию на реальную microSD карту Raspberry Pi 4 Model B

1. Отформатируйте microSD карту в файловую систему FAT32 (MBR):
   ```bash
   sudo mkfs.vfat -F 32 -n "RPI4_UEFI" /dev/sdX1
   ```

2. Скачайте официальный релиз UEFI прошивки Raspberry Pi 4 (EDK2 RPi4 Firmware v1.38+):
   https://github.com/pftf/RPi4/releases/latest

3. Распакуйте архив прошивки pftf/RPi4 в корень microSD карты.

4. Скопируйте все файлы из этой папки (`rpi4_sdcard/*`) с заменой на microSD карту:
   - `DiagModule.efi`
   - `EFI/BOOT/BOOTAA64.EFI`
   - `startup.nsh`
   - `config.txt`

5. Вставьте microSD карту в слот Raspberry Pi 4, подключите USB-UART переходник к GPIO 14 (TX), 15 (RX), GND
   (скорость 115200 8N1) или HDMI монитор.

6. Подайте питание 5V/3A. Модуль выполнит полную аппаратную диагностику SoC BCM2711, RAM и периферии до загрузки ОС.
