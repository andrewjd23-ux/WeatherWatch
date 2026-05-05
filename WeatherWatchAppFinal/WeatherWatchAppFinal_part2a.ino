// PART 2A — PROTOBUF HELPERS + TELEMETRY

float readFloatLE(const uint8_t* p) {
  float f;
  memcpy(&f, p, 4);
  return f;
}

bool readVarint(const uint8_t* p, uint16_t len, uint16_t& pos, uint32_t& value) {
  value = 0;
  uint8_t shift = 0;

  while (pos < len && shift < 32) {
    uint8_t b = p[pos++];
    value |= ((uint32_t)(b & 0x7F)) << shift;
    if ((b & 0x80) == 0) return true;
    shift += 7;
  }
  return false;
}

bool skipProtoValue(const uint8_t* p, uint16_t len, uint16_t& pos, uint8_t wireType) {
  uint32_t v = 0;

  switch (wireType) {
    case 0:
      return readVarint(p, len, pos, v);

    case 1:
      if (pos + 8 > len) return false;
      pos += 8;
      return true;

    case 2:
      if (!readVarint(p, len, pos, v)) return false;
      if (pos + v > len) return false;
      pos += (uint16_t)v;
      return true;

    case 5:
      if (pos + 4 > len) return false;
      pos += 4;
      return true;

    default:
      return false;
  }
}

void decodeEnvironmentMetrics(const uint8_t* p, uint16_t len) {
  uint16_t pos = 0;

  while (pos < len) {
    uint32_t key = 0;
    if (!readVarint(p, len, pos, key)) return;

    uint32_t field = key >> 3;
    uint8_t wireType = key & 0x07;

    if (wireType == 5 && pos + 4 <= len) {
      float f = readFloatLE(&p[pos]);
      pos += 4;

      if (field == 1 && f > -30.0 && f < 60.0) temperatureC = f;
      else if (field == 2 && f >= 0.0 && f <= 100.0) humidityPct = f;
      else if (field == 3 && f > 850.0 && f < 1100.0) pressureHpa = f;
    } else {
      if (!skipProtoValue(p, len, pos, wireType)) return;
    }
  }
}

void decodeDeviceMetricsPayload(const uint8_t* p, uint16_t len) {
  uint16_t pos = 0;

  while (pos < len) {
    uint32_t key = 0;
    if (!readVarint(p, len, pos, key)) return;

    uint32_t field = key >> 3;
    uint8_t wireType = key & 0x07;

    if (field == 1 && wireType == 0) {
      uint32_t v = 0;
      if (readVarint(p, len, pos, v) && v <= 100) batteryPct = v;
    } else if (field == 2 && wireType == 5 && pos + 4 <= len) {
      float f = readFloatLE(&p[pos]);
      pos += 4;
      if (f > 2.5 && f < 6.0) voltage = f;
    } else {
      if (!skipProtoValue(p, len, pos, wireType)) return;
    }
  }
}

void decodeTelemetryPayload(const uint8_t* p, uint16_t len) {
  uint16_t pos = 0;

  while (pos < len) {
    uint32_t key = 0;
    if (!readVarint(p, len, pos, key)) return;

    uint32_t field = key >> 3;
    uint8_t wireType = key & 0x07;

    if (wireType == 2) {
      uint32_t l = 0;
      if (!readVarint(p, len, pos, l)) return;

      if (pos + l > len) return;

      if (field == 2) decodeDeviceMetricsPayload(&p[pos], l);
      if (field == 3) decodeEnvironmentMetrics(&p[pos], l);

      pos += l;
    } else {
      if (!skipProtoValue(p, len, pos, wireType)) return;
    }
  }
}
