// PART 3 — UI / DISPLAY (FINAL)

// Only change vs previous:
// Move mail icon to center of header

void drawHeader(const char* title) {
  display.setTextSize(1);

  int centerX = 132; // midpoint of 264px width

  display.setCursor(6, 12);
  display.print(meshConnected ? "*" : "!");

  int titleWidth = strlen(title) * 6;
  display.setCursor(centerX - titleWidth/2, 12);
  display.print(title);

  // centered mail icon for messages page
  if (currentPage == 1) {
    drawMailIcon(centerX + titleWidth/2 + 4, 4);
  }

  display.setCursor(218, 12);
  display.print(currentPage + 1);
  display.print("/");
  display.print(PAGE_COUNT);

  display.drawLine(0, 22, 264, 22, GxEPD_BLACK);
}

// Rest unchanged from previous working part3
