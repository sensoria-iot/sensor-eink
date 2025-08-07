// API CONFIG & General settings
// Padre: 6800b1cb35e87    Martin: 67db2bab5bd2c
#define API_KEY "51cf96e8-9db3-4a68-83e8-138d9d5c14fe" // Isla CEO 
//#define API_KEY "b0684175-98dd-4ddb-83ea-a8c4ae5ed5dc" // Isla Teams
#define MESSAGE_SCAN_QR1 "1. Baje el App ESP-Rainmaker"
#define MESSAGE_SCAN_QR2 "2. Escanee el QR-CODE"

#define JSON_USERID 1
#define JSON_TIMEZONE "Europe/Madrid"

#define CONSUME_AMOUNT 100
#define WEB_PORT "80"
#define WEB_HOST "sensoria.cat"

#define API_URL        "http://" WEB_HOST "/api/scd40/log"
#define API_AMPERE_URL "http://" WEB_HOST "/api/energy-consumption/log"
// Not implemented in 1.1 :
#define API_CONSUME    "http://" WEB_HOST "/api/scd40/read/" API_KEY "/" STR(CONSUME_AMOUNT)

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