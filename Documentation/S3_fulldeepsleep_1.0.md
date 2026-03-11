# S3-OTA-fast-1.4 (Sensoria C5) — Firmware Overview

This version describes the first implementation based on full-deep sleep (All off except RTC). There will be an upgraded documentation for the next version (RTC wakes up MCU from deepsleep)
This document describes the main firmware flow implemented in:

- `main/CO2-read/S3-OTA-fast-1.4.cpp`

It is an ESP-IDF app for the **Sensoria C5** device, built around:
- CO₂/Temperature/Humidity sampling (Sensirion SCD4x over I²C)
- E-ink UI rendering (FastEPD, 4bpp grayscale)
- Espressif RainMaker provisioning + cloud connectivity + OTA
- HTTP API reporting (send measurements + receive server-side “analysis” payload)
- RTC alarm scheduling (via `bb_rtc`)

---

## 1) High-level boot flow

On each boot, the device:

1. Asserts the **power-hold GPIO** immediately so the board stays powered.
2. Initializes the **e-paper panel** and framebuffer.
3. Initializes NVS and loads `sensor_id` (device identity).
4. Reads **SCD4x measurements** (CO₂ ppm, temperature, humidity) and draws them on screen.
5. Initializes the **RTC** and reads time (for timestamp + alarm scheduling).
6. Starts **RainMaker + network** (connect if already provisioned; otherwise start provisioning).
7. If provisioning is active, it renders a **QR code** on the e-paper to allow a phone app to claim/provision the device.
8. Once connected (MQTT publish event), it builds JSON and **POSTs data to an API**.
9. Parses the JSON response and **draws an “analysis report” UI** on the e-paper.
10. Schedules the next RTC alarm wake-up time.
11. Cuts board power by de-asserting `POWER_HOLD_PIN`.

---

## 2) Power control (power-hold)

### Purpose
The firmware controls a “power latch” pin which keeps the board powered after boot and then cuts power at the end of the cycle.

### Key functions
- `power_hold_drive(true)` is called at the beginning of `app_main()` to keep power ON.
- `deep_sleep()` delays briefly and then calls `power_hold_drive(false)` to cut power.

> Note: despite the function name `deep_sleep()`, the current implementation cuts power through the latch. (The ESP deep sleep call is commented out.)

---

## 3) Display/UI (FastEPD)

### Purpose
FastEPD is used to render:
- sensor values (CO₂, temperature, humidity)
- a server-driven “analysis report”
- QR provisioning UI (RainMaker provisioning QR)

### Initialization
In `app_main()`:
- `epaper = new FASTEPD();`
- `epaper->initPanel(...)`, `setPanelSize(...)`, `setRotation(...)`
- `epaper->setMode(BB_MODE_4BPP)` for 16 grayscale levels
- screen clear and default text color setup

### Common render helpers
- `scd_render_co2(...)`, `scd_render_temp(...)`, `scd_render_h(...)` draw the measurement row + icons.
- `draw_response_analisis(...)` draws the “analysis report” UI using data from the API response.

---

## 4) Sensor read (Sensirion SCD4x)

### Purpose
Reads environmental data:
- `co2` (ppm)
- `tem` (°C)
- `hum` (%RH)

### Key function
- `scd_read()`

### Behavior
- Initializes Sensirion I²C HAL.
- Wakes and resets the sensor, starts periodic measurement.
- Polls readiness, reads a sample, formats values.
- Powers down SCD4x after sampling.
- Draws the measurement results to the e-paper.

---

## 5) RTC integration (bb_rtc)

### Purpose
The RTC is used for:
- reading current time to include in the JSON report (`rtc_t`)
- scheduling next wake-up via alarm
- “once-per-day” logic (e.g. firmware update check)

### Key calls
- `rtc.init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, true, 100000)`
- `rtc.getTime(&RTCTime)`
- `rtc.setTime(&cTime)` (based on server-provided datetime)
- `rtc.setAlarm(ALARM_TIME, &aTime)` (based on server-provided alarm)

### Alarm scheduling helper
- `schedule_rtc_wakeup_minutes(int minutes)` schedules a fallback wake-up after HTTP failure.

---

## 6) RainMaker: provisioning + cloud connection + OTA

### Purpose
RainMaker is used for:
- provisioning (BLE-based claim + QR payload display)
- cloud connectivity (MQTT)
- OTA enablement (RainMaker OTA service)
- receiving device “Power” commands and a custom “WiFi reset” slider parameter

### RainMaker instantiation (in `app_main()`)
Key steps:
1. `app_network_init()` initializes the networking subsystem (provision/connect).
2. Event handlers are registered:
   - `RMAKER_COMMON_EVENT`
   - `APP_NETWORK_EVENT`
3. RainMaker node created:
   - `esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Sensoria");`
4. RainMaker device created:
   - `esp_rmaker_temp_sensor_device_create("Sensoria C5", NULL, tem);`
5. A callback is attached:
   - `esp_rmaker_device_add_cb(..., write_cb, ...)`
6. A custom Wi-Fi reset parameter is created and added:
   - brightness slider named `DEVICE_PARAM_WIFI_RESET`
7. OTA enabled:
   - `esp_rmaker_ota_enable_default();`
8. RainMaker started:
   - `esp_rmaker_start();`
9. Network start:
   - `app_network_start(POP_TYPE_RANDOM);`

### RainMaker write callback
- `write_cb(...)` handles:
  - `ESP_RMAKER_DEF_POWER_NAME`: if set to false, the firmware cuts power (`deep_sleep()`).
  - `DEVICE_PARAM_WIFI_RESET`: if slider reaches 100, triggers Wi-Fi reset (`esp_rmaker_wifi_reset(...)`).

---

## 7) Provisioning QR rendering (deferred drawing pattern)

### Problem addressed
QR rendering cannot safely run directly inside the provisioning callback context (stack/time constraints). The firmware uses a two-step approach:
- callback copies QR module data into a buffer
- a dedicated worker task performs the expensive drawing

### Components
- `g_qr` stores QR size + module bitmap and a `pending` flag.
- `esp_qrcode_print_eink(...)` copies modules and notifies the worker task.
- `qr_draw_task(...)` draws the QR and updates only a display region.

### Trigger
In `event_handler_rmk(...)`, for `APP_NETWORK_EVENT_QR_DISPLAY`, RainMaker provides the QR payload string, and the firmware calls:
- `esp_qrcode_generate(&cfg_qr, (const char *)event_data);`

---

## 8) HTTP client: sending measurements to your API

### Purpose
The firmware posts measurement + metadata to a server and receives a JSON response containing:
- sleep interval (`sleep_minutes`)
- analysis values (wellbeing, benefit, confidence, etc.)
- next alarm time
- a status message for display

### JSON build
- `build_request_json()` generates the JSON payload using `json_generator`.

It includes a `client` object with:
- `key` (sensor id)
- `rtc_t` (timestamp)
- `model`, `version`
- `ip`
- `mac`
- `batt_level`

### HTTP POST
- `send_data_to_api()`:
  - uses `esp_http_client` with `_http_event_handler` and an allocated response buffer
  - POSTs JSON to `API_URL`
  - on failure schedules a fallback wake-up (`schedule_rtc_wakeup_minutes(10)`)
  - then parses and renders the response

---

## 9) Parsing server response and drawing “analysis report”

### Parse stage
- `parse_json(const char* json_string)` uses `cJSON` to extract values like:
  - `sleep_minutes`, `tipo`, `confianza`
  - multiple `confiable_*` flags
  - wellbeing/benefit metrics
  - alert prediction fields
  - server message
  - `datetime` and `alarm` objects

It also updates:
- `nvs_minutes_till_refresh` (future power-cycle interval)
- RTC current time (`rtc.setTime(&cTime)`) — typically once per day
- next wake-up alarm (`rtc.setAlarm(...)`)

### Draw stage
- `draw_response_analisis(res_tipo)` renders the response to the e-paper:
  - benefit/wellbeing metrics
  - confidence visualization
  - next predicted alert and delay
  - a “NEXT WAKEUP … VER …” line with alarm time + firmware version

---

## 10) Firmware update check (server-driven)

### Purpose
Once per day, the firmware queries a firmware endpoint and compares:
- current `firmware_version` vs server `version`.

### Flow
- `check_and_update_firmware()` is called when the day changes (based on RTC day vs alarm day).
- `check_firmware_update(update_url, ...)` calls:
  - `GET http://<WEB_HOST>/api/firmware/C5/<sensor_id>`
  - expects JSON: `{ version, status, path }`
- If update available and status OK:
  - UI shows “Updating firmware…”
  - `perform_ota_update(url)` uses `esp_https_ota(...)`
  - on success restarts the ESP

> Note: The code uses HTTP-style URLs and sets `skip_cert_common_name_check = true`. If you move to HTTPS with proper cert validation, this can be tightened.

---

## 11) Logging and debugging

The firmware uses:
- `ESP_LOGI/ESP_LOGE/ESP_LOGD` for structured logging
- `printf()` for some lifecycle messages and RTC/alarm debug output

Key debug points:
- HTTP event handler `_http_event_handler` logs chunks and connection events
- RainMaker event handler logs provisioning/claim/OTA events
- RTC/alarm prints show alarm scheduling decisions

---

## 12) Known “integration gotchas” (practical notes)

- **QR drawing must be deferred**: drawing inside the provisioning callback can crash due to stack/timing.
- **One shared I²C bus is preferred**: sensor + RTC + other peripherals should share a single I²C master instance (avoid bit-banging or repeated driver init).
- **`struct tm` conventions**: RTC libraries expect `tm_year` as years since 1900 and `tm_mon` as 0–11. Server-provided datetime/alarm values must be normalized before calling `setTime()`/`setAlarm()`.

---

## Appendix: Key tasks and state flags

- `ready_to_measure`: set when RainMaker MQTT publish event indicates connectivity; used as “Wi-Fi ready” gate.
- `measure_taken`: set when SCD4x data becomes available; used to gate RainMaker initialization flow.
- `qr_draw_task_handle`: handle for QR worker task.
- `g_qr.pending`: indicates QR bitmap is ready to be rendered.

---