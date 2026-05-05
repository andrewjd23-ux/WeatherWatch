// -----------------------------------------------------------------------------
// ROTARY / INPUT
// -----------------------------------------------------------------------------

void readEncoder() {
  int a = digitalRead(ENC_A);

  if (a != lastA && a == LOW && millis() - lastTurnMs > TURN_DEBOUNCE_MS) {
    int b = digitalRead(ENC_B);

    if (b == HIGH) {
      nextPage();
    } else {
      prevPage();
    }

    lastTurnMs = millis();
  }

  lastA = a;
}

void readButton() {
  static bool lastSw = HIGH;

  bool sw = digitalRead(ENC_SW);

  if (lastSw == HIGH && sw == LOW && millis() - lastButtonMs > 400) {
    if (currentPage == 5) {
      togglePhoneBridgeMode();
    } else {
      drawPage();
    }

    lastButtonMs = millis();
  }

  lastSw = sw;
}

void nextPage() {
  currentPage = (currentPage + 1) % PAGE_COUNT;
  drawPage();
}

void prevPage() {
  currentPage = (currentPage + PAGE_COUNT - 1) % PAGE_COUNT;
  drawPage();
}

// -----------------------------------------------------------------------------
// DISPLAY
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// UI HELPERS
// -----------------------------------------------------------------------------

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

void drawMailIcon(int x, int y) {
  display.drawRect(x, y, 18, 12, GxEPD_BLACK);
  display.drawLine(x, y, x + 9, y + 7, GxEPD_BLACK);
  display.drawLine(x + 18, y, x + 9, y + 7, GxEPD_BLACK);
}

void drawFooter() {
  display.drawLine(0, 166, 264, 166, GxEPD_BLACK);

  display.setTextSize(1);

  display.setCursor(6, 178);
  display.print(phoneBridgeMode ? "PHONE BRIDGE" : linkStatus);

  display.setCursor(130, 178);
  display.print("Stations ");
  display.print(WiFi.softAPgetStationNum());
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

// -----------------------------------------------------------------------------
// PAGES
// -----------------------------------------------------------------------------

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
  drawHeader("MESSAGES");

  drawMailIcon(222, 6);

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

  line(42, "Model", deviceModel);
  line(62, "Firmware", firmwareVersion);
  line(82, "AP", AP_SSID);
  line(102, "AP IP", WiFi.softAPIP().toString());
  line(122, "Stations", String(WiFi.softAPgetStationNum()));
  line(146, "Press", phoneBridgeMode ? "resume WeatherWatch" : "phone bridge mode");
}
