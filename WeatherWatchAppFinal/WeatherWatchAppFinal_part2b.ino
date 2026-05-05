// PART 2B — DATA + WALKER

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
    }
    else if (field == 2 && wireType == 2) {
      uint32_t l = 0;
      if (!readVarint(p, len, pos, l)) return false;
      if (pos + l > len) return false;

      payload = &p[pos];
      payloadLen = l;
      pos += l;
    }
    else {
      if (!skipProtoValue(p, len, pos, wireType)) return false;
    }
  }

  if (payload == NULL) return false;

  if (portnum == 67) {
    decodeTelemetryPayload(payload, payloadLen);
    return true;
  }

  if (portnum == 1) {
    String text = payloadToText(payload, payloadLen);
    if (text.length() > 0) addMessage(text, "LongFast", 0);
    return true;
  }

  return false;
}

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

      scanNestedMessages(&p[pos], l, depth + 1);
      pos += l;
    } else {
      if (!skipProtoValue(p, len, pos, wireType)) return;
    }
  }
}

void scanTelemetry(uint8_t* p, uint16_t len) {
  scanNestedMessages(p, len, 0);
}
