// PART 2B — FRAME GLUE / STRING LOGIC / PROTO WALKER / FALLBACK METRICS

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

  if (s.indexOf("Midnight Moses") >= 0) ownNodeName = "Midnight Moses";
  if (s.indexOf("FUNK") >= 0) shortName = "FUNK";
  if (s.indexOf("2.7.") >= 0) firmwareVersion = s.substring(0, min(20, (int)s.length()));

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
  for (uint16_t i = 0; i < s.length(); i++) {
    if (s[i] == ' ') spaces++;
  }

  if (spaces >= 2) return true;
  if (s.length() > 24 && spaces >= 1) return true;

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
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) letters++;
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

  if (messageCount > 0 && messages[0].text == text) return;

  int limit = min(messageCount, MAX_MESSAGES - 1);
  for (int i = limit; i > 0; i--) {
    messages[i] = messages[i - 1];
  }

  if (messageCount < MAX_MESSAGES) messageCount++;

  messages[0].text = text.substring(0, 60);
  messages[0].from = from;
  messages[0].channel = channel;
  messages[0].seenMs = millis();
}

// -----------------------------------------------------------------------------
// DATA MESSAGE DECODER
// -----------------------------------------------------------------------------

String payloadToText(const uint8_t* p, uint16_t len) {
  String s = "";

  for (uint16_t i = 0; i < len; i++) {
    char c = (char)p[i];
    if (c >= 32 && c <= 126) s += c;
  }

  s.trim();
  return s;
}

bool tryDecodeDataMessage(const uint8_t* p, uint16_t len) {
  uint16_t pos = 0;

  uint32_t portnum = 0xFFFFFFFF;
  const uint8_t* payload = NULL;
  uint16_t payloadLen = 0;

  while (pos < len) {
    uint32_t key = 0;
    if (!readVarint(p, len, pos, key)) return false;

    uint32_t field = key >> 3;
    uint8_t wireType = key & 0x07;

    if (field == 1 && wireType == 0) {
      uint32_t v = 0;
      if (!readVarint(p, len, pos, v)) return false;
      portnum = v;
    } else if (field == 2 && wireType == 2) {
      uint32_t l = 0;
      if (!readVarint(p, len, pos, l)) return false;
      if (pos + l > len) return false;

      payload = &p[pos];
      payloadLen = (uint16_t)l;
      pos += (uint16_t)l;
    } else {
      if (!skipProtoValue(p, len, pos, wireType)) return false;
    }
  }

  if (payload == NULL) return false;

  // TELEMETRY_APP = 67
  if (portnum == 67) {
    decodeTelemetryPayload(payload, payloadLen);
    return true;
  }

  // TEXT_MESSAGE_APP = 1
  if (portnum == 1) {
    String text = payloadToText(payload, payloadLen);
    if (text.length() > 0) addMessage(text, "LongFast", 0);
    return true;
  }

  return false;
}

// -----------------------------------------------------------------------------
// RECURSIVE PROTOBUF WALKER
// -----------------------------------------------------------------------------

void scanNestedMessages(const uint8_t* p, uint16_t len, uint8_t depth) {
  if (depth > 6 || len < 2) return;

  tryDecodeDataMessage(p, len);

  uint16_t pos = 0;

  while (pos < len) {
    uint32_t key = 0;
    if (!readVarint(p, len, pos, key)) return;

    uint8_t wireType = key & 0x07;

    if (wireType == 2) {
      uint32_t l = 0;
      if (!readVarint(p, len, pos, l)) return;
      if (pos + l > len) return;

      scanNestedMessages(&p[pos], (uint16_t)l, depth + 1);
      pos += (uint16_t)l;
    } else {
      if (!skipProtoValue(p, len, pos, wireType)) return;
    }
  }
}

// -----------------------------------------------------------------------------
// TELEMETRY ENTRY + LEGACY STUB
// -----------------------------------------------------------------------------

void scanTelemetry(uint8_t* p, uint16_t len) {
  scanNestedMessages(p, len, 0);
}

void scanEnvironmentBlock(uint8_t* p, uint16_t start, uint16_t blockLen, uint16_t frameLen) {
  // Retained for the prototype in part1; no longer used by final decoder.
}

// -----------------------------------------------------------------------------
// DEVICE / RADIO FALLBACK METRICS
// -----------------------------------------------------------------------------

void scanDeviceMetrics(uint8_t* p, uint16_t len) {
  // Fallback only. Proper DeviceMetrics is decoded inside TELEMETRY_APP.
  for (uint16_t i = 0; i + 1 < len; i++) {
    if (p[i] == 0x08 && p[i + 1] > 0 && p[i + 1] <= 100) {
      batteryPct = p[i + 1];
    }
  }
}

void scanRadioMetrics(uint8_t* p, uint16_t len) {
  // Still heuristic until MeshPacket radio fields are explicitly decoded.
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
