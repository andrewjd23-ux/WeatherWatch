#include <WiFi.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <math.h>

// -----------------------------------------------------------------------------
// WeatherWatch
// ESP32 e-ink terminal for a Heltec V3 Meshtastic weather node.
//
// Current design:
// - This ESP32 creates the WeatherWatch Wi-Fi AP.
// - The Heltec joins the AP and is expected at 192.168.4.2.
// - This sketch opens TCP 4403, sends want_config_id, and reads framed protobuf.
// - It uses a pragmatic parser for now rather than a full Meshtastic protobuf stack.
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
// These are intentionally swapped from the original physical A/B labels because
// this gave correct direction during bench testing.
#define ENC_A   33
#define ENC_B   32
#define ENC_SW  25

const int PAGE_COUNT = 6;
int currentPage = 0;

int lastA = HIGH;
unsigned long lastTurnMs = 0;
const unsigned long TURN_DEBOUNCE_MS = 300;  // slow, tactile quarter-turn feel

// ---------- MESHTASTIC ----------
WiFiClient meshClient;

// want_config_id request copied from a known-good Python CLI --listen debug run.
const uint8_t WANT_CONFIG[] = {
  0x94, 0xC3, 0x00, 0x06,
  0x18, 0x8F, 0xEA, 0xE7, 0xDD, 0x0A
};

bool meshConnected = false;
unsigned long lastConnectAttempt = 0;
unsigned long lastFrameMs = 0;
unsigned long lastRefreshMs = 0;
unsigned long lastButtonMs = 0;

// ---------- DATA MODEL ----------
String ownNodeName = "Midnight Moses";
String lastNodeName = "None";
String lastMessage = "No LongFast msg";
int nodesSeen = 0;

float temperatureC = NAN;
float humidityPct = NAN;
float pressureHpa = NAN;
float voltage = NAN;
float batteryPct = NAN;
float snr = NAN;

String linkStatus = "Booting";
String radioStatus = "No frames yet";

// ---------- PROTOTYPES ----------
void drawPage();
void readEncoder();
void readButton();
void connectMesh();
void readMeshFrames();
void handleFrame(uint8_t* p, uint16_t len);
void parseStrings(uint8_t* p, uint16_t len);
void considerString(String s);
void scanFloats(uint8_t* p, uint16_t len);
void scanInts(uint8_t* p, uint16_t len);
void pageStatus();
void pageWeather();
void pagePower();
void pageMessages();
void pageNodes();
void pageRadio();

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(800);

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastA = digitalRead(ENC_A);

  display.init(115200, true, 2, false);
  display.setRotation(1);

  drawPage();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PSK);

  linkStatus = "AP up";
  drawPage();

  delay(8000); // give the Heltec time to join the AP
  connectMesh();
}

// ---------- LOOP ----------
void loop() {
  readEncoder();
  readButton();

  if (!meshClient.connected()) {
    meshConnected = false;
    linkStatus = "Mesh TCP down";

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

// ---------- CONNECT ----------
void connectMesh() {
  lastConnectAttempt = millis();

  Serial.println("Connecting to Heltec...");
  linkStatus = "Connecting...";
  drawPage();

  meshClient.stop();

  if (!meshClient.connect(HELTEC_IP, HELTEC_PORT, 8000)) {
    Serial.println("Mesh TCP failed");
    linkStatus = "TCP failed";
    drawPage();
    return;
  }

  Serial.println("Mesh TCP connected");
  meshConnected = true;
  linkStatus = "TCP connected";

  meshClient.write(WANT_CONFIG, sizeof(WANT_CONFIG));
  Serial.println("Sent want_config_id");

  drawPage();
}

// ---------- FRAME READER ----------
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
        if (b == 0x94) state = WAIT_C3;
        break;

      case WAIT_C3:
        if (b == 0xC3) state = LEN_HI;
        else state = WAIT_94;
        break;

      case LEN_HI:
        len = ((uint16_t)b) << 8;
        state = LEN_LO;
        break;

      case LEN_LO:
        len |= b;
        pos = 0;

        if (len == 0 || len > sizeof(payload)) {
          state = WAIT_94;
        } else {
          state = PAYLOAD;
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

// ---------- FRAME HANDLING ----------
void handleFrame(uint8_t* p, uint16_t len) {
  lastFrameMs = millis();
  radioStatus = "Frames flowing";

  if (len == 0) return;

  if (p[0] == 0x22 || p[0] == 0x12 || p[0] == 0x1A || p[0] == 0x6A) {
    parseStrings(p, len);
    scanFloats(p, len);
    scanInts(p, len);
  }

  if (p[0] == 0x12) {
    radioStatus = "Live packet";
  }

  if (millis() - lastRefreshMs > 5000) {
    drawPage();
  }
}

// ---------- ROUGH PARSERS ----------
void parseStrings(uint8_t* p, uint16_t len) {
  String current = "";

  for (uint16_t i = 0; i < len; i++) {
    char c = (char)p[i];

    if (c >= 32 && c <= 126) {
      current += c;
    } else {
      considerString(current);
      current = "";
    }
  }

  considerString(current);
}

void considerString(String s) {
  s.trim();
  if (s.length() < 3) return;

  if (s.indexOf("Midnight Moses") >= 0) ownNodeName = "Midnight Moses";
  if (s.indexOf("FUNK") >= 0) ownNodeName = "Midnight Moses";

  if (s.indexOf("LongFast") >= 0 || s.indexOf("LONG_FAST") >= 0) {
    lastMessage = "LongFast activity";
  }

  // Node names and user strings often appear as readable snippets in 0x22/0x12 frames.
  if (s.startsWith("!") || s.length() > 4) {
    if (s.indexOf("proto") < 0 &&
        s.indexOf("WeatherWatch") < 0 &&
        s.indexOf("holymoses") < 0 &&
        s.indexOf("mqtt") < 0 &&
        s.indexOf("meshtastic.pool") < 0) {
      lastNodeName = s;
      nodesSeen++;
    }
  }
}

float readFloatLE(const uint8_t* p) {
  float f;
  memcpy(&f, p, 4);
  return f;
}

void scanFloats(uint8_t* p, uint16_t len) {
  // Targeted scan for protobuf float32 tags seen in telemetry-like blocks.
  // This is still a pragmatic parser rather than a full protobuf decoder.
  for (uint16_t i = 0; i + 5 <= len; i++) {
    uint8_t tag = p[i];
    float f = readFloatLE(&p[i + 1]);

    if (!isfinite(f)) continue;

    if (tag == 0x15) {
      // In observed environment metrics, this often captures temperature-like values.
      if (f > -30.0 && f < 60.0 && fabs(f) > 0.2) {
        temperatureC = f;
      }
    }

    if (tag == 0x1D) {
      if (f >= 0.0 && f <= 100.0) {
        humidityPct = f;
      } else if (f > 850.0 && f < 1100.0) {
        pressureHpa = f;
      }
    }

    if (tag == 0x25) {
      if (f > 850.0 && f < 1100.0) {
        pressureHpa = f;
      } else if (f > 2.5 && f < 6.0) {
        voltage = f;
      } else if (f >= 0.0 && f <= 100.0 && isnan(humidityPct)) {
        humidityPct = f;
      }
    }

    // Fallback: catch obvious pressure values anywhere in a telemetry frame.
    if (f > 850.0 && f < 1100.0) {
      pressureHpa = f;
    }
  }
}

void scanInts(uint8_t* p, uint16_t len) {
  // Crude battery percent heuristic. Later this should be decoded from the
  // correct protobuf field rather than inferred from any small byte.
  for (uint16_t i = 0; i < len; i++) {
    if (p[i] > 0 && p[i] <= 100) {
      batteryPct = p[i];
    }
  }
}

// ---------- ROTARY ----------
void readEncoder() {
  int a = digitalRead(ENC_A);

  if (a != lastA && a == LOW) {
    if (millis() - lastTurnMs > TURN_DEBOUNCE_MS) {
      int b = digitalRead(ENC_B);

      if (b == HIGH) nextPage();
      else prevPage();

      lastTurnMs = millis();
    }
  }

  lastA = a;
}

void readButton() {
  static bool lastSw = HIGH;
  bool sw = digitalRead(ENC_SW);

  if (lastSw == HIGH && sw == LOW) {
    if (millis() - lastButtonMs > 400) {
      drawPage();
      lastButtonMs = millis();
    }
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

// ---------- DISPLAY ----------
void drawPage() {
  lastRefreshMs = millis();

  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    drawHeader();

    switch (currentPage) {
      case 0: pageStatus(); break;
      case 1: pageWeather(); break;
      case 2: pagePower(); break;
      case 3: pageMessages(); break;
      case 4: pageNodes(); break;
      case 5: pageRadio(); break;
    }

  } while (display.nextPage());
}

void drawHeader() {
  display.setTextSize(1);
  display.setCursor(6, 14);
  display.print("WEATHERWATCH");

  display.setCursor(218, 14);
  display.print("P");
  display.print(currentPage + 1);

  display.drawLine(0, 24, 264, 24, GxEPD_BLACK);
}

void bigTitle(const char* icon, const char* title) {
  display.setTextSize(2);
  display.setCursor(10, 48);
  display.print(icon);
  display.print(" ");
  display.println(title);
}

void smallLine(int y, const String& label, const String& value) {
  display.setTextSize(1);
  display.setCursor(12, y);
  display.print(label);
  display.print(": ");
  display.println(value);
}

String f1(float v, const char* suffix) {
  if (isnan(v)) return "--";
  return String(v, 1) + suffix;
}

void pageStatus() {
  bigTitle("*", "STATUS");
  smallLine(82, "Node", ownNodeName);
  smallLine(102, "Board link", meshConnected ? "CONNECTED" : "DOWN");
  smallLine(122, "Mesh", radioStatus);
  smallLine(142, "Last frame", String((millis() - lastFrameMs) / 1000) + "s ago");
}

void pageWeather() {
  bigTitle("~", "WEATHER");
  smallLine(82, "Temp", f1(temperatureC, " C"));
  smallLine(104, "Humidity", f1(humidityPct, " %"));
  smallLine(126, "Pressure", f1(pressureHpa, " hPa"));

  display.setTextSize(1);
  display.setCursor(12, 154);
  if (!isnan(pressureHpa) && pressureHpa < 995) display.println("Low pressure. Watch sky.");
  else if (!isnan(pressureHpa) && pressureHpa > 1020) display.println("High pressure. Fair.");
  else display.println("Conditions steady.");
}

void pagePower() {
  bigTitle("+", "POWER");
  smallLine(86, "Battery", isnan(batteryPct) ? "--" : String(batteryPct, 0) + "%");
  smallLine(108, "Voltage", f1(voltage, " V"));
  smallLine(130, "Device", "Heltec V3");
}

void pageMessages() {
  bigTitle("#", "MESSAGES");
  smallLine(86, "LongFast", lastMessage);
  smallLine(126, "Canned reply", "disabled");
}

void pageNodes() {
  bigTitle("@", "NODES");
  smallLine(86, "Spotted", String(nodesSeen));
  smallLine(108, "Latest", lastNodeName);
}

void pageRadio() {
  bigTitle("/", "RADIO LINK");
  smallLine(86, "Board link", meshConnected ? "TCP OK" : "TCP DOWN");
  smallLine(108, "Mesh SNR", f1(snr, " dB"));
  smallLine(130, "Heltec IP", "192.168.4.2");
}
