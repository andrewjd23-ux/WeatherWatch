// PART 3 — UI / DISPLAY (FINAL)

// Updated header: centered title and cleaner mail icon placement

void drawHeader(const char* title) {
  display.setTextSize(1);

  int centerX = 132; // midpoint of 264px width

  // connection indicator on left
  display.setCursor(6, 12);
  display.print(meshConnected ? "*" : "!");

  // center the title
  int titleWidth = strlen(title) * 6;
  int titleX = centerX - titleWidth / 2;
  display.setCursor(titleX, 12);
  display.print(title);

  // place mail icon just after the title, avoiding page number clash
  if (currentPage == 1) {
    drawMailIcon(titleX + titleWidth + 6, 4);
  }

  // page numbers on right
  display.setCursor(218, 12);
  display.print(currentPage + 1);
  display.print("/");
  display.print(PAGE_COUNT);

  // underline
  display.drawLine(0, 22, 264, 22, GxEPD_BLACK);
}

// Rest unchanged from previous working part3
