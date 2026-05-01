#include <WiFi.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <math.h>

// -----------------------------------------------------------------------------
// WeatherWatchApp
// Experimental app-style Meshtastic terminal for the WeatherWatch ESP32.
//
// This keeps the proven WeatherWatch AP + TCP 4403 route, but reshapes the UI
// and data model to behave more like a small Meshtastic app:
//
// 1. Home
// 2. Messages
// 3. Nodes
// 4. Telemetry
// 5. Radio
// 6. Device
//
// Rotary turns pages. Encoder press forces a redraw.
// -----------------------------------------------------------------------------

// ---------- WIFI AP ----------
const char* AP_SSID = "WeatherWatch";
const char* AP_PSK  = "holymoses";

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
void readMeshFrames();
void handleFrame(uint8_t* p, uint16_t len);
void parseFrameStrings(uint8_t frameType, uint8_t* p, uint16_t len);
void classifyString(uint8_t frameType, String s);
void addNodeName(String name);
void addMessage(String text, String from, uint8_t channel);
void scanFloats(uint8_t* p, uint16_t len);
void scanInts(uint8_t* p, uint16_t len);
void scanRadioMetrics(uint8_t* p, uint16_t len);
bool looksLikeNodeName(const String& s);
bool looksLikeMessageText(const String& s);
float readFloatLE(const uint8_t* p);
void readEncoder();
void readButton();
void nextPage();
void prevPage();
void drawPage();
void drawHeader(const char* title);
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

void loop() {
  readEncoder();
  readButton();

  if (!meshClient.connected()) {
    meshConnected = false;
    linkStatus = "TCP down";
    if (millis() - lastConnectAttempt > 10000) connectMesh();
  } else {
    meshConnected = true;
    readMeshFrames();
  }

  if (millis() - lastRefreshMs > 30000) drawPage();
}

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
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PSK);
  linkStatus = "AP up";
}

void connectMesh() {
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

void readMeshFrames() {
  static enum { WAIT_94, WAIT_C3, LEN_HI, LEN_LO, PAYLOAD } state = WAIT_94;
  static uint8_t payload[2048];
  static uint16_t len = 0;
  static uint16_t pos = 0;

  while (meshClient.available()) {
    uint8_t b = meshClient.read();

    switch (state) {
      case WAIT_94:
        if (b == 0x94) state = WAIT_C3;
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
        state = (len > 0 && len <= sizeof(payload)) ? PAYLOAD : WAIT_94;
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

void handleFrame(uint8_t* p, uint16_t len) {
  if (len == 0) return;

  framesSeen++;
  lastFrameMs = millis();
  radioStatus = "Frames flowing";

  uint8_t frameType = p[0];

  if (frameType == 0x12) radioStatus = "Live packet";
  if (frameType == 0x1A) deviceModel = "heltec-v3x";
  if (frameType == 0x6A) parseFrameStrings(frameType, p, len);

  if (frameType == 0x22 || frameType == 0x12 || frameType == 0x1A || frameType == 0x6A) {
    parseFrameStrings(frameType, p, len);
    scanFloats(p, len);
    scanInts(p, len);
    scanRadioMetrics(p, len);
  }

  if (millis() - lastRefreshMs > 5000) drawPage();
}

void parseFrameStrings(uint8_t frameType, uint8_t* p, uint16_t len) {
  String current = "";

  for (uint16_t i = 0; i < len; i++) {
    char c = (char)p[i];
    if (c >= 32 && c <= 126) {
      current += c;
    } else {
      classifyString(frameType, current);
      current = "";
    }
  }

  classifyString(frameType, current);
}

void classifyString(uint8_t frameType, String s) {
  s.trim();
  if (s.length() < 3) return;

  if (s.indexOf("Midnight Moses") >= 0) ownNodeName = "Midnight Moses";
  if (s.indexOf("FUNK") >= 0) shortName = "FUNK";
  if (s.indexOf("2.7.") >= 0) firmwareVersion = s.substring(0, min(20, (int)s.length()));

  // Node info frames observed as 0x22. Live packet/message frames observed as 0x12.
  // This split prevents LongFast message text becoming a fake latest node.
  if (frameType == 0x12 && looksLikeMessageText(s)) {
    addMessage(s, "LongFast", 0);
    return;
  }

  if (frameType == 0x22 && looksLikeNodeName(s)) {
    addNodeName(s);
    return;
  }
}

bool looksLikeMessageText(const String& s) {
  if (s.indexOf("LongFast") >= 0 || s.indexOf("LONG_FAST") >= 0) return true;

  int spaces = 0;
  for (uint16_t i = 0; i < s.length(); i++) if (s[i] == ' ') spaces++;

  if (spaces >= 2) return true;
  if (s.length() > 24 && spaces >= 1) return true;
  return false;
}

bool looksLikeNodeName(const String& s) {
  if (s.length() < 3 || s.length() > 28) return false;
  if (s.startsWith("!")) return false;
  if (s.indexOf("proto") >= 0) return false;
  if (s.indexOf("WeatherWatch") >= 0) return false;
  if (s.indexOf("holymoses") >= 0) return false;
  if (s.indexOf("mqtt") >= 0) return false;
  if (s.indexOf("meshtastic.pool") >= 0) return false;
  if (s.indexOf("/") >= 0) return false;

  int letters = 0;
  for (uint16_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letters++;
  }

  return letters >= 2;
}

void addNodeName(String name) {
  name.trim();
  if (name.length() == 0) return;

  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].name == name) {
      nodes[i].seenMs = millis();
      if (!isnan(snr)) nodes[i].snr = snr;
      if (!isnan(rssi)) nodes[i].rssi = rssi;
      if (!isnan(batteryPct)) nodes[i].battery = (int)batteryPct;
      return;
    }
  }

  int idx = nodeCount;
  if (nodeCount < MAX_NODES) {
    nodeCount++;
  } else {
    idx = MAX_NODES - 1;
    for (int i = 1; i < MAX_NODES; i++) nodes[i - 1] = nodes[i];
  }

  nodes[idx].name = name.substring(0, 28);
  nodes[idx].seenMs = millis();
  nodes[idx].snr = snr;
  nodes[idx].rssi = rssi;
  nodes[idx].battery = isnan(batteryPct) ? -1 : (int)batteryPct;
}

void addMessage(String text, String from, uint8_t channel) {
  text.trim();
  if (text.length() == 0) return;

  if (messageCount > 0 && messages[0].text == text) return;

  int limit = min(messageCount, MAX_MESSAGES - 1);
  for (int i = limit; i > 0; i--) messages[i] = messages[i - 1];
  if (messageCount < MAX_MESSAGES) messageCount++;

  messages[0].text = text.substring(0, 60);
  messages[0].from = from;
  messages[0].channel = channel;
  messages[0].seenMs = millis();
}

float readFloatLE(const uint8_t* p) {
  float f;
  memcpy(&f, p, 4);
  return f;
}

void scanFloats(uint8_t* p, uint16_t len) {
  for (uint16_t i = 0; i + 5 <= len; i++) {
    uint8_t tag = p[i];
    float f = readFloatLE(&p[i + 1]);
    if (!isfinite(f)) continue;

    if (tag == 0x15 && f > -30.0 && f < 60.0 && fabs(f) > 0.2) temperatureC = f;
    if (tag == 0x1D && f >= 0.0 && f <= 100.0) humidityPct = f;
    if (tag == 0x1D && f > 850.0 && f < 1100.0) pressureHpa = f;
    if (tag == 0x25 && f > 850.0 && f < 1100.0) pressureHpa = f;
    if (tag == 0x25 && f > 2.5 && f < 6.0) voltage = f;
    if (f > 850.0 && f < 1100.0) pressureHpa = f;
  }
}

void scanInts(uint8_t* p, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    if (p[i] > 0 && p[i] <= 100) batteryPct = p[i];
  }
}

void scanRadioMetrics(uint8_t* p, uint16_t len) {
  for (uint16_t i = 0; i + 5 <= len; i++) {
    uint8_t tag = p[i];
    float f = readFloatLE(&p[i + 1]);
    if (!isfinite(f)) continue;

    if (tag == 0x45 || tag == 0x4D || tag == 0x55) {
      if (f >= -30.0 && f <= 30.0) snr = f;
      else if (f >= -140.0 && f <= -20.0) rssi = f;
    }
  }
}

void readEncoder() {
  int a = digitalRead(ENC_A);
  if (a != lastA && a == LOW && millis() - lastTurnMs > TURN_DEBOUNCE_MS) {
    int b = digitalRead(ENC_B);
    if (b == HIGH) nextPage();
    else prevPage();
    lastTurnMs = millis();
  }
  lastA = a;
}

void readButton() {
  static bool lastSw = HIGH;
  bool sw = digitalRead(ENC_SW);
  if (lastSw == HIGH && sw == LOW && millis() - lastButtonMs > 400) {
    drawPage();
    lastButtonMs = millis();
  }
  lastSw = sw;
}

void nextPage() {
  currentPage = (currentPage + 1) % PAGE_COUNT;
  drawPage();
}

void prevPage() {
  currentPage--;
  if (currentPage < 0) currentPage = PAGE_COUNT - 1;
  drawPage();
}

void drawPage() {
  lastRefreshMs = millis();
  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    switch (currentPage) {
      case 0: pageHome(); break;
      case 1: pageMessages(); break;
      case 2: pageNodes(); break;
      case 3: pageTelemetry(); break;
      case 4: pageRadio(); break;
      case 5: pageDevice(); break;
    }

    drawFooter();
  } while (display.nextPage());
}

void drawHeader(const char* title) {
  display.setTextSize(1);
  display.setCursor(6, 12);
  display.print(meshConnected ? "*" : "!");
  display.print(" ");
  display.print(title);
  display.setCursor(218, 12);
  display.print(currentPage + 1);
  display.print("/");
  display.print(PAGE_COUNT);
  display.drawLine(0, 22, 264, 22, GxEPD_BLACK);
}

void drawFooter() {
  display.drawLine(0, 166, 264, 166, GxEPD_BLACK);
  display.setTextSize(1);
  display.setCursor(6, 178);
  display.print(linkStatus);
  display.setCursor(130, 178);
  display.print("Frames ");
  display.print(framesSeen);
}

void line(int y, const String& label, const String& value) {
  display.setTextSize(1);
  display.setCursor(10, y);
  display.print(label);
  display.print(": ");
  display.println(value);
}

String f1(float v, const char* suffix) {
  if (isnan(v)) return "--";
  return String(v, 1) + suffix;
}

String ago(unsigned long whenMs) {
  if (whenMs == 0) return "never";
  unsigned long sec = (millis() - whenMs) / 1000;
  if (sec < 60) return String(sec) + "s";
  if (sec < 3600) return String(sec / 60) + "m";
  return String(sec / 3600) + "h";
}

void pageHome() {
  drawHeader("HOME");
  display.setTextSize(2);
  display.setCursor(10, 48);
  display.println(ownNodeName);
  display.setTextSize(1);
  line(82, "Short", shortName);
  line(102, "Status", radioStatus);
  line(122, "Last frame", ago(lastFrameMs) + " ago");
  line(142, "Nodes", String(nodeCount));
}

void pageMessages() {
  drawHeader("LONGFAST");
  display.setTextSize(1);
  if (messageCount == 0) {
    display.setCursor(10, 60);
    display.println("No candidate messages yet.");
    return;
  }
  int y = 42;
  for (int i = 0; i < min(messageCount, 4); i++) {
    display.setCursor(10, y);
    display.print(i + 1);
    display.print(" ");
    display.print(ago(messages[i].seenMs));
    display.print(" ");
    display.println(messages[i].text.substring(0, 31));
    y += 28;
  }
}

void pageNodes() {
  drawHeader("NODES");
  display.setTextSize(1);
  if (nodeCount == 0) {
    display.setCursor(10, 60);
    display.println("No node names decoded yet.");
    return;
  }
  int y = 38;
  int start = max(0, nodeCount - 5);
  for (int i = start; i < nodeCount; i++) {
    display.setCursor(10, y);
    display.print("- ");
    display.print(nodes[i].name.substring(0, 24));
    display.setCursor(210, y);
    display.print(ago(nodes[i].seenMs));
    y += 24;
  }
}

void pageTelemetry() {
  drawHeader("TELEMETRY");
  display.setTextSize(2);
  display.setCursor(10, 46);
  display.print(f1(temperatureC, " C"));
  display.setTextSize(1);
  line(86, "Humidity", f1(humidityPct, " %"));
  line(106, "Pressure", f1(pressureHpa, " hPa"));
  line(126, "Battery", isnan(batteryPct) ? "--" : String(batteryPct, 0) + "%");
  line(146, "Voltage", f1(voltage, " V"));
}

void pageRadio() {
  drawHeader("RADIO");
  line(48, "Board link", meshConnected ? "TCP OK" : "TCP DOWN");
  line(70, "Mesh metric", f1(snr, " dB"));
  line(92, "RSSI", f1(rssi, " dBm"));
  line(114, "Heltec IP", "192.168.4.2");
  line(136, "Last packet", ago(lastFrameMs) + " ago");
}

void pageDevice() {
  drawHeader("DEVICE");
  line(48, "Model", deviceModel);
  line(70, "Firmware", firmwareVersion);
  line(92, "AP", AP_SSID);
  line(114, "AP IP", WiFi.softAPIP().toString());
  line(136, "Stations", String(WiFi.softAPgetStationNum()));
}
