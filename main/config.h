// API CONFIG & General settings
// Please apply for an API Key in sensoria.cat
#define API_KEY "6f46d584-fb4e-4141-af7a-5033dbd80f07" // test KEY

#define MESSAGE_SCAN_QR1 "1. Baje el App ESP-Rainmaker"
#define MESSAGE_SCAN_QR2 "2. Escanee el QR-CODE"

#define JSON_USERID 1
#define JSON_TIMEZONE "Europe/Madrid"

#define WEB_PORT "80"
#define WEB_HOST "sensoria.cat"

#define API_URL        "http://" WEB_HOST "/api/scd40/log"

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
