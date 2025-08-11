# cs-embedded-v2

Bu proje Zephyr RTOS üzerinde STM32F103 için hazırlanmıştır. Projeyi derlemek ve cihaza yüklemek için aşağıdaki adımları takip ediniz.

---

## 📦 Zephyr Ortamının Kurulumu

1. [Zephyr Başlangıç Kılavuzu](https://docs.zephyrproject.org/latest/develop/getting_started/index.html) adresindeki talimatlara göre Zephyr ortamını kurun.



2. **STM32CubeProgrammer** uygulamasını indirin:  
   👉 [STMicroelectronics STM32CubeProgrammer](https://www.st.com/en/development-tools/flasher-stm32.html#getsoftware-scroll)

3. (Opsiyonel) CLI'den erişim için STM32CubeProgrammer'ın `bin` klasörünü `Path` ortam değişkenine ekleyin:

   ```powershell
    [Environment]::SetEnvironmentVariable("Path", "$([Environment]::GetEnvironmentVariable('Path','User'));C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin", "User")   
    ```

4. ZEPHYR_ROOT ve ZEPHYR_ENV ortam değişkenini kalıcı olarak ayarlayın:

   ```powershell
   [Environment]::SetEnvironmentVariable("ZEPHYR_ROOT", "C:\Users\Admin\zephyrproject\zephyr", "User")
   [Environment]::SetEnvironmentVariable("ZEPHYR_ENV", "C:\Users\Admin\zephyrproject\.venv\Scripts\Activate.ps1", "User")

   ```

---

## ⚙️ Derleme

Otomatik olarak derlemek için gerekli dizinleri `build.ps1` dosyasını düzenleyerek çalıştırıp derleyebilirsiniz. Yada aşağıdaki manuel yöntemi kullanabilirsiniz. Derleme tamamlandıktan sonra eğer include hataları alırsanız .vscode\settings.json dosyasında compile_commands.json bildirilmiş olduğundan emin olun 

settings.json görünümü şu şekilde olmalı
```json
   {
    "C_Cpp.default.compileCommands": "${workspaceFolder}/.build/compile_commands.json",
   }
```



1. `west` ortamını aktif edin:

   ```bash
   & $env:ZEPHYR_ENV      
   ```

3. Derleme işlemi:

   ```bash
    cd ..\cs-embedded-v2
    west build -b stm32f103_mini . --build-dir .build  -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   ```

---

## 🔌 Firmware Yükleme

Aşağıdaki komutla `zephyr.bin` dosyasını STM32'ye UART üzerinden yükleyebilirsiniz:

```powershell
STM32_Programmer_CLI.exe -c port=COM7 baudrate=115200 -w build\zephyr\zephyr.bin 0x08000000 -v -rst
```

> `build/zephyr/zephyr.bin` dosyasının yolunu kontrol edin.

---

## 🔍 Seri Port İzleme (Windows)

### Seçenek 1: PlatformIO veya PuTTY
Dilerseniz [PlatformIO](https://platformio.org/install/ide?install=vscode) veya PuTTY ile `COM` portu izleyebilirsiniz.

### Seçenek 2: Plink (Putty CLI)

1. Plink'i indirin:

   ```powershell
   Invoke-WebRequest -Uri "https://the.earth.li/~sgtatham/putty/latest/w64/plink.exe" -OutFile "$env:USERPROFILE\.plink\plink.exe"
   ```

2. Ortam değişkenine ekleyin:

   ```powershell
   [Environment]::SetEnvironmentVariable("Path", "$([Environment]::GetEnvironmentVariable('Path','User'));$env:USERPROFILE\.plink", "User")
   ```

3. Seri portu izle:

   ```powershell
   plink -serial COM7 -sercfg 115200,8,n,1,N
   ```

---

## 🛠 Gereksinimler

- Python 3.8+
- Zephyr SDK + west
- STM32CubeProgrammer
- UART-USB dönüştürücü (ör. CP2102)

---

## 📝 Notlar



- `$env:ZEPHYR_BASE="C:\Users\Admin\Developer\sdk\zephyr\zephyrproject\zephyr"`
- `$env:ZEPHYR_SDK_INSTALL_DIR="C:\Users\Admin\Developer\sdk\zephyr\zephyr-sdk-0.17.2"`
- `C:\Users\Admin\Developer\sdk\zephyr\zephyrproject\.venv\Scripts\Activate.ps1`
- `C:\Windows\system32\cmd.exe /d /s /c "west build -b stm32f4_disco  . --build-dir .build  -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DEXTRA_DTC_OVERLAY_FILE=stm32f4_disco_stm32f407x.overlay"`