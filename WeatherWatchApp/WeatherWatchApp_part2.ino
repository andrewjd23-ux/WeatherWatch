// -----------------------------------------------------------------------------
// FRAME HANDLING
// -----------------------------------------------------------------------------

void handleFrame(uint8_t* p, uint16_t len) {
  if (len == 0) return;

  framesSeen++;
  lastFrameMs = millis();
  radioStatus = "Frames flowing";

  uint8_t frameType = p[0];

  if (frameType == 0x12) {
    radioStatus = "Live packet";
  }

  if (frameType == 0x1A) {
    deviceModel = "heltec-v3x";
  }

  if (frameType == 0x22 || frameType == 0x12 || frameType == 0x1A || frameType == 0x6A) {
    parseFrameStrings(frameType, p, len);
    scanTelemetry(p, len);
    scanDeviceMetrics(p, len);
    scanRadioMetrics(p, len);
  }

  if (millis() - lastRefreshMs > 5000) {
    drawPage();
  }
}

// -----------------------------------------------------------------------------
// STRING PARSING
// -----------------------------------------------------------------------------

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

  if (s.indexOf("Midnight Moses") >= 0) {
    ownNodeName = "Midnight Moses";
  }

  if (s.indexOf("FUNK") >= 0) {
    shortName = "FUNK";
  }

  if (s.indexOf("2.7.") >= 0) {
    firmwareVersion = s.substring(0, min(20, (int)s.length()));
  }

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
  if (s.indexOf("LongFast") >= 0 || s.indexOf("LONG_FAST") >= 0) {
    return true;
  }

  int spaces = 0;

  for (uint16_t i = 0; i < s.length(); i++) {
    if (s[i] == ' ') spaces++;
  }

  if (spaces >= 2) return true;

  if (s.length() > 24 && spaces >= 1) {
    return true;
  }

  return false;
}

bool looksLikeNodeName(const String& s) {
  if (s.length() < 3) return false;
  if (s.length() > 28) return false;

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

    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
      letters++;
    }
  }

  return letters >= 2;
}

// -----------------------------------------------------------------------------
// NODE / MESSAGE STORAGE
// -----------------------------------------------------------------------------

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

    for (int i = 1; i < MAX_NODES; i++) {
      nodes[i - 1] = nodes[i];
    }
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

  if (messageCount > 0 && messages[0].text == text) {
    return;
  }

  int limit = min(messageCount, MAX_MESSAGES - 1);

  for (int i = limit; i > 0; i--) {
    messages[i] = messages[i - 1];
  }

  if (messageCount < MAX_MESSAGES) {
    messageCount++;
  }

  messages[0].text = text.substring(0, 60);
  messages[0].from = from;
  messages[0].channel = channel;
  messages[0].seenMs = millis();
}

// -----------------------------------------------------------------------------
// BINARY HELPERS
// -----------------------------------------------------------------------------

float readFloatLE(const uint8_t* p) {
  float f;

  memcpy(&f, p, 4);

  return f;
}

// -----------------------------------------------------------------------------
// TELEMETRY PARSING
// -----------------------------------------------------------------------------

void scanTelemetry(uint8_t* p, uint16_t len) {
  // Looser scan for EnvironmentMetrics float fields anywhere in telemetry-ish frames.
  // temperature = 0x0D
  // humidity    = 0x15
  // pressure    = 0x1D

  float t = NAN;
  float h = NAN;
  float pr = NAN;

  for (uint16_t i = 0; i + 5 <= len; i++) {
    uint8_t tag = p[i];
    float f = readFloatLE(&p[i + 1]);

    if (!isfinite(f)) continue;

    if (tag == 0x0D && f > -30.0 && f < 60.0 && fabs(f) > 0.1) {
      t = f;
    }

    if (tag == 0x15 && f >= 1.0 && f <= 100.0) {
      // Avoid taking 4.3V as humidity by preferring humidity-looking values.
      if (f > 10.0) h = f;
    }

    if (tag == 0x1D && f > 850.0 && f < 1100.0) {
      pr = f;
    }
  }

  if (!isnan(t)) temperatureC = t;
  if (!isnan(h)) humidityPct = h;
  if (!isnan(pr)) pressureHpa = pr;
}

void scanEnvironmentBlock(uint8_t* p, uint16_t start, uint16_t blockLen, uint16_t frameLen) {
  // Kept because the prototype references it.
  // scanTelemetry() now performs the direct loose scan.
}

// -----------------------------------------------------------------------------
// DEVICE / RADIO METRICS
// -----------------------------------------------------------------------------

void scanDeviceMetrics(uint8_t* p, uint16_t len) {
  for (uint16_t i = 0; i + 5 <= len; i++) {
    uint8_t tag = p[i];

    float f = readFloatLE(&p[i + 1]);

    if (!isfinite(f)) continue;

    if (tag == 0x15 && f > 2.5 && f < 6.0) {
      voltage = f;
    }
  }

  for (uint16_t i = 0; i + 1 < len; i++) {
    if (p[i] == 0x08 && p[i + 1] > 0 && p[i + 1] <= 100) {
      batteryPct = p[i + 1];
    }
  }
}

void scanRadioMetrics(uint8_t* p, uint16_t len) {
  for (uint16_t i = 0; i + 5 <= len; i++) {
    uint8_t tag = p[i];

    float f = readFloatLE(&p[i + 1]);

    if (!isfinite(f)) continue;

    if (tag == 0x45 || tag == 0x4D || tag == 0x55) {
      if (f >= -30.0 && f <= 30.0) {
        snr = f;
      } else if (f >= -140.0 && f <= -20.0) {
        rssi = f;
      }
    }
  }
}
