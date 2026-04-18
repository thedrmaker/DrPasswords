//Gmail TOTP
const char* WIFI_SSID = "***************";
const char* WIFI_PASSWORD = "**********";
const char* GMAIL_TOTP_BASE32 = "aaaa bbbb cccc dddd eeee ffff gggg hhhh";

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>
#include <TOTP.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

TFT_eSPI tft = TFT_eSPI();
USBHIDKeyboard Keyboard;
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

// ===== Buttons =====
#define BTN_OK_UP   0
#define BTN_DOWN    14

// ===== Wi-Fi / Time =====
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const long GMT_OFFSET_SEC = 3 * 3600;   // Saudi Arabia
const int DAYLIGHT_OFFSET_SEC = 0;

uint8_t gmailTotpSecret[64];
size_t gmailTotpSecretLen = 0;
TOTP* gmailTotp = nullptr;

// ===== Timing =====
const unsigned long DEBOUNCE_MS = 180;
const unsigned long HOLD_TIME_MS = 700;
const unsigned long USB_STARTUP_DELAY_MS = 800;   // was 3000
const byte DNS_PORT = 53;

// ===== Menu =====
enum ItemType {
  ITEM_SUBMENU,
  ITEM_TEXT,
  ITEM_TOTP,
  ITEM_INFO
};

struct MenuItem {
  const char* name;
  ItemType type;
  const char* textValue;
  TOTP* totp;
};

enum MenuScreen {
  SCREEN_MAIN,
  SCREEN_PASSWORDS,
  SCREEN_PASSWORD_ACTIONS,
  SCREEN_TOTP,
  SCREEN_TOTP_VIEW,
  SCREEN_SETTINGS
};

MenuScreen currentScreen = SCREEN_MAIN;
int selectedIndex = 0;
int menuScrollOffset = 0;

MenuItem mainMenuItems[] = {
  {"Passwords", ITEM_SUBMENU, nullptr, nullptr},
  {"TOTP",      ITEM_SUBMENU, nullptr, nullptr},
  {"Settings",  ITEM_SUBMENU, nullptr, nullptr}
};

const int MAX_PASSWORD_ITEMS = 12;

MenuItem passwordMenuItems[MAX_PASSWORD_ITEMS];
int passwordMenuCount = 4;

char passwordNameValues[MAX_PASSWORD_ITEMS][32] = {
  "Gmail",
  "GitHub",
  "AWS",
  "Bank"
};

MenuItem passwordActionMenuItems[] = {
  {"Username",   ITEM_INFO, nullptr, nullptr},
  {"Password",   ITEM_INFO, nullptr, nullptr},
  {"UserTabPass", ITEM_INFO, nullptr, nullptr}
};

MenuItem totpMenuItems[] = {
  {"Google TOTP", ITEM_TOTP, nullptr, nullptr}
};

MenuItem settingsMenuItems[] = {
};

MenuItem* getCurrentMenuItems() {
  switch (currentScreen) {
    case SCREEN_PASSWORDS:         return passwordMenuItems;
    case SCREEN_PASSWORD_ACTIONS:  return passwordActionMenuItems;
    case SCREEN_TOTP:              return totpMenuItems;
    case SCREEN_SETTINGS:          return settingsMenuItems;
    case SCREEN_MAIN:
    default:                       return mainMenuItems;
  }
}

int getCurrentMenuCount() {
  switch (currentScreen) {
    case SCREEN_PASSWORDS:        return passwordMenuCount;
    case SCREEN_PASSWORD_ACTIONS: return sizeof(passwordActionMenuItems) / sizeof(passwordActionMenuItems[0]);
    case SCREEN_TOTP:             return sizeof(totpMenuItems) / sizeof(totpMenuItems[0]);
    case SCREEN_SETTINGS:         return 0;
    case SCREEN_MAIN:
    default:                      return sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);
  }
}

const char* getScreenTitle() {
  switch (currentScreen) {
    case SCREEN_PASSWORDS:        return "Passwords";
    case SCREEN_PASSWORD_ACTIONS: return "Send As";
    case SCREEN_TOTP:             return "TOTP";
    case SCREEN_TOTP_VIEW:        return "TOTP";
    case SCREEN_SETTINGS:         return "Settings";
    case SCREEN_MAIN:
    default:                      return "Dr. Passwords";
  }
}

// ===== Time sync state =====
bool timeSynced = false;
int activeTotpIndex = 0;
unsigned long lastTotpRedraw = 0;
bool settingsPortalActive = false;
String settingsApPassword;
int activePasswordIndex = 0;

char passwordTextValues[MAX_PASSWORD_ITEMS][128] = {
  "MyGmailPassword123",
  "MyGitHubPassword456",
  "MyAwsPassword789",
  "MyBankPassword000"
};

char passwordUsernameValues[MAX_PASSWORD_ITEMS][128] = {
  "user.alpha01",
  "bluefalcon77",
  "cloud.user23",
  "banking_hero",
  "gamma.node",
  "pixel.rider",
  "tiger_login",
  "neo.account",
  "orbit.user",
  "delta.signin",
  "fastlane.id",
  "quantum.user"
};

// ===== Button states =====
bool lastDownState = HIGH;
bool btnPressed = false;
bool holdTriggered = false;
unsigned long btnPressStart = 0;
unsigned long lastDownPress = 0;
bool downHoldHandled = false;

// ===== Base32 decode (for TOTP) =====
int base32Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '2' && c <= '7') return c - '2' + 26;
  return -1;
}

size_t decodeBase32(const char* input, uint8_t* output, size_t maxOutputLen) {
  int buffer = 0;
  int bitsLeft = 0;
  size_t outLen = 0;

  while (*input) {
    char c = *input++;
    if (c == ' ' || c == '=' || c == '-') continue;

    int val = base32Value(c);
    if (val < 0) continue;

    buffer = (buffer << 5) | val;
    bitsLeft += 5;

    if (bitsLeft >= 8) {
      bitsLeft -= 8;
      if (outLen >= maxOutputLen) return 0;
      output[outLen++] = (buffer >> bitsLeft) & 0xFF;
    }
  }
  return outLen;
}

bool initTotp() {
  gmailTotpSecretLen = decodeBase32(GMAIL_TOTP_BASE32, gmailTotpSecret, sizeof(gmailTotpSecret));
  if (gmailTotpSecretLen == 0) return false;
  gmailTotp = new TOTP(gmailTotpSecret, gmailTotpSecretLen);
  return true;
}

// ===== UI =====
void drawMenu() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(0xFCC0, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(12, 10);
  tft.println(getScreenTitle());
  tft.setTextSize(2);

  MenuItem* items = getCurrentMenuItems();
  int itemCount = getCurrentMenuCount();

  const int startY = 55;
  const int lineHeight = 28;
  const int visibleItems = 4;

  if (selectedIndex < menuScrollOffset) {
    menuScrollOffset = selectedIndex;
  }
  if (selectedIndex >= menuScrollOffset + visibleItems) {
    menuScrollOffset = selectedIndex - visibleItems + 1;
  }
  if (menuScrollOffset < 0) menuScrollOffset = 0;
  if (itemCount <= visibleItems) menuScrollOffset = 0;

  int endIndex = menuScrollOffset + visibleItems;
  if (endIndex > itemCount) endIndex = itemCount;

  int y = startY;
  for (int i = menuScrollOffset; i < endIndex; i++) {
    if (i == selectedIndex) {
      tft.fillRect(8, y - 2, 300, 22, TFT_LIGHTGREY);
      tft.setTextColor(TFT_GREEN, TFT_LIGHTGREY);
      tft.setCursor(12, y);
      tft.print("> ");
      tft.println(items[i].name);
    } else {
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setCursor(12, y);
      tft.print("  ");
      tft.println(items[i].name);
    }
    y += lineHeight;
  }

  if (menuScrollOffset > 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(290, 40);
    tft.print("^");
  }

  if (endIndex < itemCount) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(290, 118);
    tft.print("v");
  }
}

void showSendingScreen(const char* text) {
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 45);
  tft.println("Sending...");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(20, 85);
  tft.println(text);
}

bool syncTimeOverWiFi() {
  WiFi.mode(WIFI_STA);

  while (true) {
    WiFi.disconnect(true);
    delay(200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
      configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);

      struct tm timeinfo;
      start = millis();
      while (millis() - start < 20000) {
        if (getLocalTime(&timeinfo)) {
          WiFi.disconnect(true);
          WiFi.mode(WIFI_OFF);
          return true;
        }
        delay(250);
      }
    }

    WiFi.disconnect(true);
    delay(500);
  }
}

bool getCurrentUnixTime(time_t &unixTime) {
  time(&unixTime);
  return unixTime > 1700000000;
}

void showStatusScreen(const char* title, const char* line2) {
  const int titleY = (tft.height() / 2) - 20;
  const int dotsY = titleY + 30;

  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(0xFCC0, TFT_BLACK);
  tft.setTextSize(2);

  int titleWidth = strlen(title) * 12;
  int titleX = (tft.width() - titleWidth) / 2;
  if (titleX < 0) titleX = 0;

  tft.setCursor(titleX, titleY);
  tft.println(title);

  const char* dots = "...........";
  int dotsLen = strlen(dots);
  int phase = (millis() / 180) % ((dotsLen * 2) - 2);
  int visibleCount = (phase < dotsLen) ? (phase + 1) : ((dotsLen * 2) - phase - 1);

  int dotsWidth = visibleCount * 12;
  int dotsX = (tft.width() - dotsWidth) / 2;
  if (dotsX < 0) dotsX = 0;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(dotsX, dotsY);

  for (int i = 0; i < visibleCount; i++) {
    tft.print('.');
  }
}

void showTotpCodeScreen(const char* title, const char* code, int secondsRemaining) {
  const int period = 30;

  int barWidth = 180;
  int barX = (tft.width() - barWidth) / 2;
  int progressWidth = (secondsRemaining * barWidth) / period;
  if (progressWidth < 0) progressWidth = 0;
  if (progressWidth > barWidth - 4) progressWidth = barWidth - 4;

  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  const char* displayTitle = title;
  if (strcmp(title, "Gmail TOTP") == 0) {
    displayTitle = "Google";
  }

  int titleWidth = strlen(displayTitle) * 12;
  int titleX = (tft.width() - titleWidth) / 2;
  if (titleX < 0) titleX = 0;
  tft.setCursor(titleX, 12);
  tft.println(displayTitle);

  int boxWidth = 200;
  int boxHeight = 54;
  int boxX = (tft.width() - boxWidth) / 2;
  int boxY = 42;

  tft.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 10, TFT_LIGHTGREY);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(4);

  int charWidth = 24;
  int codeWidth = strlen(code) * charWidth;
  int codeX = boxX + (boxWidth - codeWidth) / 2;
  int codeY = boxY + (boxHeight - 32) / 2;

  tft.setCursor(codeX, codeY);
  tft.println(code);

  int barY = 112;
  tft.drawRoundRect(barX, barY, barWidth, 12, 6, TFT_DARKGREY);
  if (progressWidth > 0) {
    tft.fillRoundRect(barX + 2, barY + 2, progressWidth, 8, 4, TFT_GREEN);
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);

  char timeText[6];
  sprintf(timeText, "%ds", secondsRemaining);

  int timeWidth = strlen(timeText) * 12;
  int timeX = barX + barWidth + 8;
  int timeY = barY - 2;

  tft.setCursor(timeX, timeY);
  tft.print(timeText);
}

String htmlEscape(const char* input) {
  String s = input ? input : "";
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

String generateApPassword() {
  const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*";
  const int passwordLength = 16;

  String result = "";
  result.reserve(passwordLength);

  for (int i = 0; i < passwordLength; i++) {
    result += charset[esp_random() % (sizeof(charset) - 1)];
  }

  return result;
}

void refreshPasswordMenuItems() {
  for (int i = 0; i < passwordMenuCount; i++) {
    passwordMenuItems[i].name = passwordNameValues[i];
    passwordMenuItems[i].type = ITEM_TEXT;
    passwordMenuItems[i].textValue = passwordTextValues[i];
    passwordMenuItems[i].totp = nullptr;
  }
}

void loadSavedPasswords() {
  prefs.begin("vault", true);

  int storedCount = prefs.getInt("count", 4);
  if (storedCount < 1) storedCount = 1;
  if (storedCount > MAX_PASSWORD_ITEMS) storedCount = MAX_PASSWORD_ITEMS;
  passwordMenuCount = storedCount;

  for (int i = 0; i < passwordMenuCount; i++) {
    String nameKey = "name" + String(i);
    String userKey = "user" + String(i);
    String passKey = "pass" + String(i);

    String defaultName = passwordNameValues[i];
    String defaultUser = passwordUsernameValues[i];
    String defaultPass = passwordTextValues[i];

    String loadedName = prefs.getString(nameKey.c_str(), defaultName);
    String loadedUser = prefs.getString(userKey.c_str(), defaultUser);
    String loadedPass = prefs.getString(passKey.c_str(), defaultPass);

    loadedName.toCharArray(passwordNameValues[i], sizeof(passwordNameValues[i]));
    loadedUser.toCharArray(passwordUsernameValues[i], sizeof(passwordUsernameValues[i]));
    loadedPass.toCharArray(passwordTextValues[i], sizeof(passwordTextValues[i]));
  }

  prefs.end();
  refreshPasswordMenuItems();
}


void savePasswordsToPreferences() {
  prefs.begin("vault", false);
  prefs.clear();
  prefs.putInt("count", passwordMenuCount);

  for (int i = 0; i < passwordMenuCount; i++) {
    String nameKey = "name" + String(i);
    String userKey = "user" + String(i);
    String passKey = "pass" + String(i);
    prefs.putString(nameKey.c_str(), passwordNameValues[i]);
    prefs.putString(userKey.c_str(), passwordUsernameValues[i]);
    prefs.putString(passKey.c_str(), passwordTextValues[i]);
  }

  prefs.end();
}

void drawSettingsPortalScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(0xFCC0, TFT_BLACK);
  tft.setTextSize(2);

  const char* title = "Settings Portal";
  int titleX = (tft.width() - (strlen(title) * 12)) / 2;
  if (titleX < 0) titleX = 0;
  tft.setCursor(titleX, 10);
  tft.println(title);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(16, 42);
  tft.println("SSID:");
  tft.setCursor(78, 42);
  tft.println("Dr. Passwords");

  tft.setCursor(16, 72);
  tft.println("PASS:");
  tft.setCursor(78, 72);
  tft.println("Press OK to send");

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(16, 104);
  tft.println("Connect to WiFi and open any page.");
  tft.setCursor(16, 116);
  tft.println("Hold BACK to close portal.");
}

void handlePortalRoot() {
  int editIndex = -1;
  if (server.hasArg("edit")) {
    editIndex = server.arg("edit").toInt();
    if (editIndex < 0 || editIndex >= passwordMenuCount) {
      editIndex = -1;
    }
  }

  String formName = "";
  String formUser = "";
  String formPass = "";
  bool isEditMode = (editIndex >= 0);

  // Generate default random password for new entries
  if (!isEditMode) {
    const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*";
    for (int i = 0; i < 16; i++) {
      formPass += charset[esp_random() % (sizeof(charset) - 1)];
    }
  }

  if (isEditMode) {
    formName = htmlEscape(passwordNameValues[editIndex]);
    formUser = htmlEscape(passwordUsernameValues[editIndex]);
    formPass = "";
  }

  String html = "";
  html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Dr. Passwords</title>";
  html += "<style>";
  html += "body{margin:0;font-family:Arial,sans-serif;background:#0b0b0b;color:#f5f5f5;padding:22px;}";
  html += ".wrap{max-width:980px;margin:0 auto;}";
  html += "h1{margin:0 0 18px 0;text-align:center;font-size:28px;font-weight:700;color:#ffffff;}";
  html += ".card{background:#161616;border:1px solid rgba(255,255,255,0.08);border-radius:20px;padding:24px;margin-bottom:22px;box-shadow:0 10px 24px rgba(0,0,0,0.35);} ";
  html += "h2{margin:0 0 18px 0;font-size:18px;color:#ffffff;}";
  html += "label{display:block;margin:14px 0 8px 0;font-weight:600;color:#d7d7d7;}";
  html += "input{width:100%;padding:14px 16px;border-radius:12px;border:1px solid rgba(255,255,255,0.10);background:#0f0f0f;color:#fff;box-sizing:border-box;font-size:15px;outline:none;}";
  html += "input::placeholder{color:#8d8d8d;}";
  html += ".actions{display:flex;gap:12px;flex-wrap:wrap;margin-top:22px;}";
  html += ".btn{display:inline-block;padding:13px 22px;border:none;border-radius:12px;font-size:15px;font-weight:700;text-decoration:none;cursor:pointer;}";
  html += ".btn-primary{background:#4d8ef7;color:#fff;}";
  html += ".btn-secondary{background:#4b4b4b;color:#fff;}";
  html += ".btn-edit{background:#4d8ef7;color:#fff;padding:10px 18px;border-radius:10px;text-decoration:none;}";
  html += ".btn-delete{background:#ef4b3f;color:#fff;padding:10px 18px;border-radius:10px;text-decoration:none;display:inline-block;}";
  html += "table{width:100%;border-collapse:collapse;overflow:hidden;border-radius:14px;}";
  html += "th,td{padding:16px 14px;border:1px solid rgba(255,255,255,0.08);text-align:left;vertical-align:middle;}";
  html += "th{background:#202020;color:#f2f2f2;font-size:15px;}";
  html += "td{background:#161616;}";
  html += ".table-actions{display:flex;gap:10px;flex-wrap:wrap;align-items:center;}";
  html += ".muted{color:#bdbdbd;font-size:14px;margin-top:8px;}";
  html += "@media (max-width:700px){body{padding:14px;} .card{padding:18px;} th,td{padding:12px 10px;font-size:14px;} .table-actions{flex-direction:column;align-items:stretch;}}";
  html += "</style></head><body><div class='wrap'>";
  html += "<h1>Password Management</h1>";
  html += "<div class='card'><h2>";

  html += isEditMode ? "Edit Saved Password" : "Add New Password";
  html += "</h2><form method='POST' action='/save'>";

  if (isEditMode) {
    html += "<input type='hidden' name='edit_index' value='" + String(editIndex) + "'>";
  }

  html += "<label>Name</label>";
  html += "<input name='item_name' placeholder='Example: Gmail' value='" + formName + "'>";
  html += "<label>Username</label>";
  html += "<input name='item_user' placeholder='Enter username' value='" + formUser + "'>";
  html += "<label>Password</label>";
  html += "<input id='pwd' name='item_pass' type='text' placeholder='Enter new password' value='" + formPass + "'>";

  // Password generator UI
  html += "<div style='margin-top:16px;'>";
  html += "<div style='margin-bottom:8px;color:#cfcfcf;'>Password Length: <span id='lenVal'>16</span></div>";
  html += "<input id='len' type='range' min='4' max='64' value='16' style='width:100%;'>";
  html += "<div style='display:flex;justify-content:space-between;color:#8d8d8d;font-size:12px;'><span>4</span><span>64</span></div>";
  html += "<div style='margin-top:10px;'>";
  html += "<button type='button' class='btn btn-secondary' onclick='gen()'>Regenerate</button>";
  html += "</div></div>";

  // Inline JS for generation
  html += "<script>";
  html += "const cs='ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789!@#$%^&*';";
  html += "function gen(){var l=parseInt(document.getElementById('len').value)||16;var s='';for(var i=0;i<l;i++){s+=cs[Math.floor(Math.random()*cs.length)];}document.getElementById('pwd').value=s;document.getElementById('lenVal').textContent=l;}";
  html += "document.addEventListener('DOMContentLoaded',function(){var r=document.getElementById('len');var v=document.getElementById('lenVal');if(r&&v){v.textContent=r.value;r.addEventListener('input',function(){v.textContent=this.value;});}});";
  html += "</script>";

  html += "<div class='actions'>";
  html += "<button class='btn btn-primary' type='submit'>";
  html += isEditMode ? "Save Changes" : "Add Password";
  html += "</button>";
  if (isEditMode) {
    html += "<a class='btn btn-secondary' href='/'>Cancel</a>";
  }
  html += "</div></form></div>";

  html += "<div class='card'><h2>Saved Passwords</h2>";
  if (passwordMenuCount == 0) {
    html += "<p class='muted'>No saved passwords yet.</p>";
  } else {
    html += "<table><tr><th>Name</th><th>Username</th><th>Actions</th></tr>";
    for (int i = 0; i < passwordMenuCount; i++) {
      html += "<tr>";
      html += "<td>" + htmlEscape(passwordNameValues[i]) + "</td>";
      html += "<td>" + htmlEscape(passwordUsernameValues[i]) + "</td>";
      html += "<td><div class='table-actions'>";
      html += "<a class='btn-edit' href='/?edit=" + String(i) + "'>Edit</a>";
      html += "<a class='btn-delete' href='/delete_now?index=" + String(i) + "&ts=" + String(millis()) + "'>Delete</a>";
      html += "</div></td>";
      html += "</tr>";
    }
    html += "</table>";
  }
  html += "</div></div></body></html>";

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(200, "text/html", html);
}

void handlePortalSave() {
  String itemName = server.arg("item_name");
  String itemUser = server.arg("item_user");
  String itemPass = server.arg("item_pass");
  itemName.trim();
  itemUser.trim();
  itemPass.trim();

  if (itemName.length() == 0) {
    server.send(200, "text/html", "<!doctype html><html><body style='background:#0b0b0b;color:#fff;font-family:Arial;padding:24px;'><h2>Name is required</h2><p><a href='/' style='color:#7fc0ff;'>Back</a></p></body></html>");
    return;
  }

  int targetIndex = -1;
  if (server.hasArg("edit_index")) {
    targetIndex = server.arg("edit_index").toInt();
    if (targetIndex < 0 || targetIndex >= passwordMenuCount) {
      targetIndex = -1;
    }
  }

  if (targetIndex >= 0) {
    itemName.toCharArray(passwordNameValues[targetIndex], sizeof(passwordNameValues[targetIndex]));
    itemUser.toCharArray(passwordUsernameValues[targetIndex], sizeof(passwordUsernameValues[targetIndex]));
    if (itemPass.length() > 0) {
      itemPass.toCharArray(passwordTextValues[targetIndex], sizeof(passwordTextValues[targetIndex]));
    }
  } else {
    if (itemPass.length() == 0) {
      server.send(200, "text/html", "<!doctype html><html><body style='background:#0b0b0b;color:#fff;font-family:Arial;padding:24px;'><h2>Password is required for new items</h2><p><a href='/' style='color:#7fc0ff;'>Back</a></p></body></html>");
      return;
    }

    if (passwordMenuCount >= MAX_PASSWORD_ITEMS) {
      server.send(200, "text/html", "<!doctype html><html><body style='background:#0b0b0b;color:#fff;font-family:Arial;padding:24px;'><h2>Storage full</h2><p>Maximum password items reached.</p><p><a href='/' style='color:#7fc0ff;'>Back</a></p></body></html>");
      return;
    }

    itemName.toCharArray(passwordNameValues[passwordMenuCount], sizeof(passwordNameValues[passwordMenuCount]));
    itemUser.toCharArray(passwordUsernameValues[passwordMenuCount], sizeof(passwordUsernameValues[passwordMenuCount]));
    itemPass.toCharArray(passwordTextValues[passwordMenuCount], sizeof(passwordTextValues[passwordMenuCount]));
    passwordMenuCount++;
  }

  refreshPasswordMenuItems();
  savePasswordsToPreferences();
  handlePortalRoot();
}

void stopSettingsPortal();

void startSettingsPortal() {
  stopSettingsPortal();
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  delay(200);
  WiFi.mode(WIFI_AP);
  delay(100);

  bool apStarted = WiFi.softAP("Dr. Passwords", settingsApPassword.c_str());
  delay(300);

  if (!apStarted) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 40);
    tft.println("AP Start Failed");
    tft.setTextSize(1);
    tft.setCursor(20, 75);
    tft.println("Check AP password length");
    delay(1200);
    currentScreen = SCREEN_MAIN;
    drawMenu();
    return;
  }

  server.stop();
  dnsServer.stop();

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handlePortalRoot);
  server.on("/generate_204", HTTP_GET, handlePortalRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);
  server.on("/ncsi.txt", HTTP_GET, handlePortalRoot);
  server.on("/save", HTTP_POST, handlePortalSave);
  server.on("/delete_now", HTTP_GET, handlePortalDeleteNow);
  server.onNotFound(handlePortalRoot);
  server.begin();

  settingsPortalActive = true;
  drawSettingsPortalScreen();
}

void handlePortalDeleteNow() {
  if (!server.hasArg("index")) {
    handlePortalRoot();
    return;
  }

  int deleteIndex = server.arg("index").toInt();
  if (deleteIndex < 0 || deleteIndex >= passwordMenuCount) {
    handlePortalRoot();
    return;
  }

  for (int i = deleteIndex; i < passwordMenuCount - 1; i++) {
    strlcpy(passwordNameValues[i], passwordNameValues[i + 1], sizeof(passwordNameValues[i]));
    strlcpy(passwordUsernameValues[i], passwordUsernameValues[i + 1], sizeof(passwordUsernameValues[i]));
    strlcpy(passwordTextValues[i], passwordTextValues[i + 1], sizeof(passwordTextValues[i]));
  }

  passwordMenuCount--;
  if (passwordMenuCount < 0) passwordMenuCount = 0;

  if (passwordMenuCount < MAX_PASSWORD_ITEMS) {
    passwordNameValues[passwordMenuCount][0] = ' ';
    passwordUsernameValues[passwordMenuCount][0] = ' ';
    passwordTextValues[passwordMenuCount][0] = ' ';
  }

  if (selectedIndex >= passwordMenuCount && passwordMenuCount > 0) {
    selectedIndex = passwordMenuCount - 1;
  }
  if (selectedIndex < 0) selectedIndex = 0;

  refreshPasswordMenuItems();
  savePasswordsToPreferences();
  handlePortalRoot();
}

void stopSettingsPortal() {
  if (!settingsPortalActive) return;
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  settingsPortalActive = false;
}

void processSettingsPortal() {
  if (!settingsPortalActive) return;
  dnsServer.processNextRequest();
  server.handleClient();
}

void drawBootScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(0xFCC0, TFT_BLACK);
  tft.setTextSize(3);

  const char* title = "Dr. Passwords";

  int charWidth = 6 * 3;
  int textWidth = strlen(title) * charWidth;
  int x = (tft.width() - textWidth) / 2;
  int y = tft.height() / 2 - 12;

  tft.setCursor(x, y);
  tft.println(title);
}

void moveUp() {
  int itemCount = getCurrentMenuCount();
  selectedIndex--;
  if (selectedIndex < 0) selectedIndex = itemCount - 1;
  drawMenu();
}

void moveDown() {
  int itemCount = getCurrentMenuCount();
  selectedIndex++;
  if (selectedIndex >= itemCount) selectedIndex = 0;
  drawMenu();
}

void sendSelectedItem() {
  MenuItem* items = getCurrentMenuItems();
  MenuItem &item = items[selectedIndex];

  if (currentScreen == SCREEN_TOTP_VIEW) {
    time_t now;
    if (!getCurrentUnixTime(now)) return;

    char* code = totpMenuItems[activeTotpIndex].totp->getCode(now);
    Keyboard.print(code);
    return;
  }

  if (settingsPortalActive) {
    Keyboard.print(settingsApPassword);
    return;
  }

  if (currentScreen == SCREEN_PASSWORD_ACTIONS) {
    showSendingScreen(passwordMenuItems[activePasswordIndex].name);
    delay(250);

    if (selectedIndex == 0) {
      Keyboard.print(passwordUsernameValues[activePasswordIndex]);
    } else if (selectedIndex == 1) {
      Keyboard.print(passwordTextValues[activePasswordIndex]);
    } else if (selectedIndex == 2) {
      Keyboard.print(passwordUsernameValues[activePasswordIndex]);
      Keyboard.write(KEY_TAB);
      Keyboard.print(passwordTextValues[activePasswordIndex]);
    } else if (selectedIndex == 1) {
      Keyboard.print(passwordUsernameValues[activePasswordIndex]);
    } else if (selectedIndex == 2) {
      Keyboard.print(passwordUsernameValues[activePasswordIndex]);
      Keyboard.write(KEY_TAB);
      Keyboard.print(passwordTextValues[activePasswordIndex]);
    }

    delay(400);
    drawMenu();
    return;
  }

  if (item.type == ITEM_SUBMENU) {
    if (currentScreen == SCREEN_MAIN) {
      if (selectedIndex == 0) {
        currentScreen = SCREEN_PASSWORDS;
        selectedIndex = 0;
        menuScrollOffset = 0;
        drawMenu();
      } else if (selectedIndex == 1) {
        if (!timeSynced) {
          showStatusScreen("Connecting WiFi", "TOTP");
          syncTimeOverWiFi();
          timeSynced = true;
        }
        activeTotpIndex = 0;
        currentScreen = SCREEN_TOTP_VIEW;
        lastTotpRedraw = 0;
      } else if (selectedIndex == 2) {
        selectedIndex = 0;
        menuScrollOffset = 0;
        startSettingsPortal();
      }
    }
    return;
  }

  if (item.type == ITEM_INFO) {
    return;
  }

  if (item.type == ITEM_TEXT) {
    activePasswordIndex = selectedIndex;
    currentScreen = SCREEN_PASSWORD_ACTIONS;
    selectedIndex = 0;
    menuScrollOffset = 0;
    drawMenu();
    return;
  }

  if (item.type == ITEM_TOTP) {
    if (!timeSynced) {
      showStatusScreen("Connecting WiFi", item.name);
      syncTimeOverWiFi();
      timeSynced = true;
    }

    activeTotpIndex = selectedIndex;
    currentScreen = SCREEN_TOTP_VIEW;
    lastTotpRedraw = 0;
    return;
  }
}

void setup() {
  pinMode(BTN_OK_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  settingsApPassword = generateApPassword();


  //Uncomment For Easy Tests
  //settingsApPassword = "12345678";

  loadSavedPasswords();

  tft.init();
  tft.setRotation(1);

  if (!initTotp()) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 40);
    tft.println("TOTP Error");
    tft.setTextSize(1);
    tft.setCursor(20, 75);
    tft.println("Invalid Base32");
    while (true) delay(1000);
  }

  totpMenuItems[0].totp = gmailTotp;

  drawBootScreen();
  delay(1200);

  drawMenu();

  Keyboard.begin();
  USB.begin();

  delay(USB_STARTUP_DELAY_MS);
}

void loop() {
  processSettingsPortal();
  bool okUpState = digitalRead(BTN_OK_UP);
  bool downState = digitalRead(BTN_DOWN);
  unsigned long nowMs = millis();

  if (currentScreen == SCREEN_TOTP_VIEW) {
    time_t now;
    if (getCurrentUnixTime(now)) {
      int secondsRemaining = 30 - (now % 30);
      if (secondsRemaining == 30) secondsRemaining = 0;

      if (lastTotpRedraw != (unsigned long)now) {
        char* code = totpMenuItems[activeTotpIndex].totp->getCode(now);
        showTotpCodeScreen(totpMenuItems[activeTotpIndex].name, code, secondsRemaining);
        lastTotpRedraw = (unsigned long)now;
      }
    }
  }

  if (okUpState == LOW) {
    if (!btnPressed) {
      btnPressed = true;
      btnPressStart = nowMs;
      holdTriggered = false;
    } else {
      if (!holdTriggered && (nowMs - btnPressStart >= HOLD_TIME_MS)) {
        holdTriggered = true;
        sendSelectedItem();
      }
    }
  } else {
    if (btnPressed && !holdTriggered) {
      if (currentScreen != SCREEN_TOTP_VIEW && !settingsPortalActive) {
        moveDown();
      }
    }
    btnPressed = false;
    holdTriggered = false;
  }

  if (downState == LOW) {
    if (lastDownState == HIGH) {
      lastDownPress = nowMs;
      downHoldHandled = false;
    }

    if (!downHoldHandled && (nowMs - lastDownPress >= HOLD_TIME_MS)) {
      downHoldHandled = true;

      if (currentScreen == SCREEN_TOTP_VIEW) {
        currentScreen = SCREEN_TOTP;
        selectedIndex = 0;
        menuScrollOffset = 0;
        drawMenu();
      } else if (currentScreen == SCREEN_PASSWORD_ACTIONS) {
        currentScreen = SCREEN_PASSWORDS;
        selectedIndex = activePasswordIndex;
        drawMenu();
      } else if (settingsPortalActive) {
        stopSettingsPortal();
        currentScreen = SCREEN_MAIN;
        selectedIndex = 0;
        menuScrollOffset = 0;
        drawMenu();
      } else if (currentScreen != SCREEN_MAIN) {
        currentScreen = SCREEN_MAIN;
        selectedIndex = 0;
        menuScrollOffset = 0;
        drawMenu();
      }
    }
  } else {
    if (lastDownState == LOW && !downHoldHandled && (nowMs - lastDownPress < HOLD_TIME_MS)) {
      if (currentScreen != SCREEN_TOTP_VIEW && !settingsPortalActive) {
        moveUp();
      }
    }
    downHoldHandled = false;
  }

  lastDownState = downState;

  delay(20);
}
