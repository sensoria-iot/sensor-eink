// API CONFIG & General settings
// Please apply for an API Key in sensoria.cat
#define API_KEY "7abbcddc-88c4-4bc0-a6ff-7331494f5279"
//#define API_KEY "6f46d584-fb4e-4141-af7a-5033dbd80f07" // FASANI
// JON d6163c1e-6a0f-430a-9e01-b159b71ec9c3 // Pacific/Honolulu
// KIM b3b5c965-f420-4686-b8f5-ff8422cd5409 Adam 7abbcddc-88c4-4bc0-a6ff-7331494f5279  TIMEZONE: America/Toronto

//#define MESSAGE_SCAN_QR1 "1. Baje el App ESP-Rainmaker"
//#define MESSAGE_SCAN_QR2 "2. Escanee el QR-CODE"
#define MESSAGE_SCAN_QR1 "1 Download ESP-Rainmaker app"
#define MESSAGE_SCAN_QR2 "2 Scan the QR-CODE"

#define FORCE_WIFI_RESET 0
#define JSON_TIMEZONE "America/Toronto"

#define WEB_PORT "80"
#define WEB_HOST "sensoria.cat" // dev.

#define API_URL  "http://" WEB_HOST "/api/scd40/log"

// DISPLAY
#define EPD_WIDTH  1280
#define EPD_HEIGHT  720

// I2C
#define CONFIG_SDA_GPIO 39
#define CONFIG_SCL_GPIO 40
// Station will refresh every:
#define DEEP_SLEEP_MINUTES 30
// INTERNALS
#define MAX_HTTP_OUTPUT_BUFFER 1024
