// PART 1 — CORE / WIFI / TCP

#include <WiFi.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <math.h>

// -----------------------------------------------------------------------------
// WeatherWatchAppFinal
// Standalone ESP32 e-ink Meshtastic terminal for WeatherWatch.
// -----------------------------------------------------------------------------

// ---------- WIFI AP ----------
const char* AP_SSID = "WeatherWatch";
const char* AP_PSK  = "holymoses";

IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

IPAddress HELTEC_IP(192, 168, 4, 2);
const uint16_t HELTEC_PORT = 4403;

// ---------- E-INK ----------
#define EPD_CS    5
#define EPD_DC    17
#define EPD_RST   16
#define EPD_BUSY  4

GxEPD2_BW<GxEPD2_270_GDEY027T91, GxEPD2_270_GDEY027T91::HEIGHT> display(
  GxEPD2_270_GDEY027T91(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ---------- ROTARY ----------
#define ENC_A   33
#define ENC_B   32
#define ENC_SW  25

const int PAGE_COUNT = 6;

int currentPage = 0;
int lastA = HIGH;

unsigned long lastTurnMs = 0;
unsigned long lastButtonMs = 0;

const unsigned long TURN_DEBOUNCE_MS = 300;

// ---------- MESHTASTIC TCP ----------
WiFiClient meshClient;

const uint8_t WANT_CONFIG[] = {
  0x94, 0xC3, 0x00, 0x06,
  0x18, 0x8F, 0xEA, 0xE7, 0xDD, 0x0A
};

bool meshConnected = false;
bool phoneBridgeMode = false;

unsigned long lastConnectAttempt = 0;
unsigned long lastFrameMs = 0;
unsigned long lastRefreshMs = 0;

uint32_t framesSeen = 0;

// ---------- APP DATA ----------
struct NodeItem {
  String name;
  unsigned long seenMs;
  float snr;
  float rssi;
  int battery;
};

struct MessageItem {
  String text;
  String from;
  uint8_t channel;
  unsigned long seenMs;
};

const int MAX_NODES = 18;
const int MAX_MESSAGES = 8;

NodeItem nodes[MAX_NODES];
MessageItem messages[MAX_MESSAGES];

int nodeCount = 0;
int messageCount = 0;

String ownNodeName = "Midnight Moses";
String shortName = "FUNK";

String deviceModel = "Heltec V3";
String firmwareVersion = "--";

String linkStatus = "Booting";
String radioStatus = "No frames";

float temperatureC = NAN;
float humidityPct = NAN;
float pressureHpa = NAN;

float voltage = NAN;
float batteryPct = NAN;

float snr = NAN;
float rssi = NAN;

// ---------- PROTOTYPES ----------
void setupDisplay();
void setupInput();
void setupAp();

void connectMesh();
void disconnectMeshForPhone();
void togglePhoneBridgeMode();

void readMeshFrames();
void handleFrame(uint8_t* p, uint16_t len);

void parseFrameStrings(uint8_t frameType, uint8_t* p, uint16_t len);
void classifyString(uint8_t frameType, String s);

void addNodeName(String name);
void addMessage(String text, String from, uint8_t channel);

void scanTelemetry(uint8_t* p, uint16_t len);
void scanEnvironmentBlock(uint8_t* p, uint16_t start, uint16_t blockLen, uint16_t frameLen);

void scanDeviceMetrics(uint8_t* p, uint16_t len);
void scanRadioMetrics(uint8_t* p, uint16_t len);

bool looksLikeNodeName(const String& s);
bool looksLikeMessageText(const String& s);

float readFloatLE(const uint8_t* p);

bool readVarint(const uint8_t* p, uint16_t len, uint16_t& pos, uint32_t& value);
bool skipProtoValue(const uint8_t* p, uint16_t len, uint16_t& pos, uint8_t wireType);
String payloadToText(const uint8_t* p, uint16_t len);

void decodeEnvironmentMetrics(const uint8_t* p, uint16_t len);
void decodeDeviceMetricsPayload(const uint8_t* p, uint16_t len);
void decodeTelemetryPayload(const uint8_t* p, uint16_t len);
bool tryDecodeDataMessage(const uint8_t* p, uint16_t len);
void scanNestedMessages(const uint8_t* p, uint16_t len, uint8_t depth);

void readEncoder();
void readButton();

void nextPage();
void prevPage();

void drawPage();
void drawHeader(const char* title);
void drawMailIcon(int x, int y);
void drawFooter();

void line(int y, const String& label, const String& value);

void pageHome();
void pageMessages();
void pageNodes();
void pageTelemetry();
void pageRadio();
void pageDevice();

String f1(float v, const char* suffix);
String ago(unsigned long whenMs);

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(800);

  setupInput();
  setupDisplay();
  setupAp();

  drawPage();

  delay(8000);

  connectMesh();
}

// -----------------------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------------------

void loop() {
  readEncoder();
  readButton();

  if (phoneBridgeMode) {
    if (meshClient.connected()) {
      disconnectMeshForPhone();
    }
  } else if (!meshClient.connected()) {
    meshConnected = false;
    linkStatus = "TCP down";

    if (millis() - lastConnectAttempt > 10000) {
      connectMesh();
    }
  } else {
    meshConnected = true;
    readMeshFrames();
  }

  if (millis() - lastRefreshMs > 30000) {
    drawPage();
  }
}

// -----------------------------------------------------------------------------
// HARDWARE SETUP
// -----------------------------------------------------------------------------

void setupInput() {
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  lastA = digitalRead(ENC_A);
}

void setupDisplay() {
  display.init(115200, true, 2, false);
  display.setRotation(1);
}

void setupAp() {
  WiFi.mode(WIFI_OFF);
  delay(300);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);

  // Channel 6, visible SSID, max 4 clients: Heltec + phone + extras.
  WiFi.softAP(AP_SSID, AP_PSK, 6, 0, 4);

  delay(500);

  linkStatus = "AP up";
}

// -----------------------------------------------------------------------------
// MESH CONNECTION
// -----------------------------------------------------------------------------

void connectMesh() {
  if (phoneBridgeMode) return;

  lastConnectAttempt = millis();
  linkStatus = "Connecting";

  drawPage();

  meshClient.stop();

  if (!meshClient.connect(HELTEC_IP, HELTEC_PORT, 8000)) {
    meshConnected = false;
    linkStatus = "TCP failed";

    drawPage();

    return;
  }

  meshConnected = true;
  linkStatus = "TCP OK";

  meshClient.write(WANT_CONFIG, sizeof(WANT_CONFIG));

  drawPage();
}

void disconnectMeshForPhone() {
  meshClient.stop();
  meshConnected = false;
  linkStatus = "PHONE BRIDGE";
}

void togglePhoneBridgeMode() {
  phoneBridgeMode = !phoneBridgeMode;

  if (phoneBridgeMode) {
    disconnectMeshForPhone();
    radioStatus = "Phone owns TCP";
  } else {
    radioStatus = "Reconnecting";
    connectMesh();
  }

  drawPage();
}

// -----------------------------------------------------------------------------
// FRAME READER
// -----------------------------------------------------------------------------

void readMeshFrames() {
  static enum {
    WAIT_94,
    WAIT_C3,
    LEN_HI,
    LEN_LO,
    PAYLOAD
  } state = WAIT_94;

  static uint8_t payload[2048];

  static uint16_t len = 0;
  static uint16_t pos = 0;

  while (meshClient.available()) {
    uint8_t b = meshClient.read();

    switch (state) {
      case WAIT_94:
        if (b == 0x94) {
          state = WAIT_C3;
        }
        break;

      case WAIT_C3:
        state = (b == 0xC3) ? LEN_HI : WAIT_94;
        break;

      case LEN_HI:
        len = ((uint16_t)b) << 8;
        state = LEN_LO;
        break;

      case LEN_LO:
        len |= b;
        pos = 0;

        if (len > 0 && len <= sizeof(payload)) {
          state = PAYLOAD;
        } else {
          state = WAIT_94;
        }
        break;

      case PAYLOAD:
        payload[pos++] = b;

        if (pos >= len) {
          handleFrame(payload, len);
          state = WAIT_94;
        }
        break;
    }
  }
}
