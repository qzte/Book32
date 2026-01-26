#include "AppKlipper.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "icon_klipper.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

// Scan interval (30 seconds)
static const unsigned long SCAN_INTERVAL = 30000;
// Status update interval (10 seconds)
static const unsigned long UPDATE_INTERVAL = 10000;

AppKlipper::AppKlipper() {
    _state = KLIPPER_SCANNING;
    _selectedIndex = 0;
    _needsRedraw = true;
    _scanning = false;
    _lastScanTime = 0;
    _lastUpdateTime = 0;
    _scanProgress = 0;
    _scannedIPs = 0;
    _totalIPs = 0;
}

AppKlipper::~AppKlipper() {
    _printers.clear();
}

void AppKlipper::start() {
    Serial.println("Klipper App Started");
    _state = KLIPPER_SCANNING;
    _selectedIndex = 0;
    _needsRedraw = true;
    _scanning = true;
    _lastScanTime = 0;
    _lastUpdateTime = 0;

    InputMgr::getInstance().setCallback(std::bind(&AppKlipper::handleInput, this, std::placeholders::_1));

    // Start mDNS if not already started
    if (!MDNS.begin("book32")) {
        Serial.println("mDNS already running or failed");
    }

    // Trigger immediate scan
    scanForPrinters();
}

void AppKlipper::stop() {
    Serial.println("Klipper App Stopped");
    _printers.clear();
}

void AppKlipper::handleInput(InputAction action) {
    if (action == INPUT_NEXT) {
        if (_state == KLIPPER_SCANNING && !_scanning) {
            // Single press in scanning view: start manual scan
            scanForPrinters();
        }
        else if (_state == KLIPPER_LIST && !_printers.empty()) {
            // Cycle through printers
            _selectedIndex++;
            if (_selectedIndex >= (int)_printers.size()) {
                _selectedIndex = 0;
            }
            _needsRedraw = true;
        }
    }
    else if (action == INPUT_SELECT || action == INPUT_BACK) {
        // Any long press or back: return to main menu
        AppMgr::getInstance().switchTo(0);
    }
}

void AppKlipper::update() {
    unsigned long now = millis();

    // No auto-rescanning - user must press button to scan

    // Periodic status update in list view (partial refresh)
    if (_state == KLIPPER_LIST && !_printers.empty() && (now - _lastUpdateTime > UPDATE_INTERVAL)) {
        for (auto& printer : _printers) {
            fetchPrinterStatus(printer);
        }
        _lastUpdateTime = now;
        _needsRedraw = true;
    }
}

void AppKlipper::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    switch (_state) {
        case KLIPPER_SCANNING:
            drawScanning();
            break;
        case KLIPPER_LIST:
            drawPrinterList();
            break;
    }
}

const uint8_t* AppKlipper::getIconImage() {
    return icon_klipper_48x48;
}

void AppKlipper::scanForPrinters() {
    Serial.println("Scanning for Klipper/Moonraker printers...");
    
    // Verify WiFi is connected before scanning
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: WiFi not connected!");
        _scanning = false;
        _needsRedraw = true;
        return;
    }
    
    _scanning = true;
    _needsRedraw = true;
    draw();  // Show scanning message
    _lastScanTime = millis();

    // Keep track of existing IPs to avoid duplicates
    std::vector<String> existingIPs;
    for (const auto& p : _printers) {
        existingIPs.push_back(p.ip);
    }

    // Method 1: Try mDNS discovery
    Serial.println("Trying mDNS discovery...");
    int n = MDNS.queryService("moonraker", "tcp");
    Serial.printf("mDNS found %d Moonraker services\n", n);

    for (int i = 0; i < n; i++) {
        String ip = MDNS.IP(i).toString();

        // Check if already in list
        bool exists = false;
        for (const auto& existingIP : existingIPs) {
            if (existingIP == ip) {
                exists = true;
                break;
            }
        }

        if (!exists) {
            PrinterInfo printer;
            printer.hostname = MDNS.hostname(i);
            printer.ip = ip;
            printer.port = MDNS.port(i);
            printer.state = "unknown";
            printer.extruderTemp = 0;
            printer.extruderTarget = 0;
            printer.bedTemp = 0;
            printer.bedTarget = 0;
            printer.progress = 0;
            printer.filename = "";

            Serial.printf("mDNS found: %s at %s:%d\n", printer.hostname.c_str(), printer.ip.c_str(), printer.port);
            _printers.push_back(printer);
            existingIPs.push_back(ip);
        }
    }

    // Method 2: If mDNS found nothing, scan local subnet
    if (_printers.empty()) {
        Serial.println("mDNS found nothing, scanning subnet...");
        scanSubnet();
    }

    _scanning = false;

    // Transition to list view if we found printers
    if (!_printers.empty()) {
        _state = KLIPPER_LIST;
        // Fetch initial status for all printers
        for (auto& printer : _printers) {
            fetchPrinterStatus(printer);
        }
    }

    _needsRedraw = true;
}

void AppKlipper::scanSubnet() {
    // Verify WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, cannot scan subnet");
        return;
    }

    // Get local IP
    IPAddress localIP = WiFi.localIP();
    Serial.printf("Local IP: %s, scanning subnet for Moonraker...\n", localIP.toString().c_str());

    // Default Moonraker port
    const uint16_t MOONRAKER_PORT = 7125;

    // Get base octets from local IP
    uint8_t oct1 = localIP[0];
    uint8_t oct2 = localIP[1];
    uint8_t oct3 = localIP[2];
    uint8_t myOct4 = localIP[3];

    int found = 0;
    int scanned = 0;

    _scanProgress = 0;
    _scannedIPs = 0;
    _totalIPs = 50;  // Only scanning .100-.150

    // Only scan .100-.150 range (common DHCP range)
    // This is much faster than scanning .1-.50 + .100-.200
    for (int i = 100; i <= 150 && found < 5; i++) {
        if (i == myOct4) continue;

        IPAddress target(oct1, oct2, oct3, i);
        String targetStr = target.toString();
        _scannedIPs++;
        _scanProgress = (_scannedIPs * 100) / _totalIPs;
        
        // Update display every 5 IPs
        if (_scannedIPs % 5 == 0) {
            _needsRedraw = true;
            draw();
        }
        scanned++;

        if (probeMoonraker(targetStr, MOONRAKER_PORT)) {
            // Check if already in list
            bool exists = false;
            for (const auto& p : _printers) {
                if (p.ip == targetStr) {
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                PrinterInfo printer;
                printer.ip = targetStr;
                printer.port = MOONRAKER_PORT;
                printer.hostname = "Klipper";
                printer.state = "unknown";
                printer.extruderTemp = 0;
                printer.extruderTarget = 0;
                printer.bedTemp = 0;
                printer.bedTarget = 0;
                printer.progress = 0;
                printer.filename = "";

                // Try to get printer name from Moonraker database (where Mainsail stores it)
                // This is the custom name set in Mainsail UI
                String dbUrl = "http://" + printer.ip + ":" + String(printer.port) + "/server/database/item?namespace=mainsail";
                String dbResponse = httpGet(dbUrl);
                if (dbResponse.length() > 0) {
                    DynamicJsonDocument doc(4096);
                    if (deserializeJson(doc, dbResponse) == DeserializationError::Ok) {
                        // Try to get printer name from general settings
                        const char* printerName = doc["result"]["value"]["general"]["printername"] | nullptr;
                        if (printerName && strlen(printerName) > 0) {
                            printer.hostname = printerName;
                            Serial.printf("Got printer name from Mainsail DB: %s\n", printerName);
                        }
                    }
                }
                
                // If we didn't get a custom name, try /printer/info for hostname
                if (printer.hostname == "Klipper") {
                    String infoUrl = "http://" + printer.ip + ":" + String(printer.port) + "/printer/info";
                    String infoResponse = httpGet(infoUrl);
                    if (infoResponse.length() > 0) {
                        DynamicJsonDocument doc(2048);
                        if (deserializeJson(doc, infoResponse) == DeserializationError::Ok) {
                            const char* hostname = doc["result"]["hostname"] | "Klipper";
                            printer.hostname = hostname;
                            Serial.printf("Got hostname from /printer/info: %s\n", hostname);
                        }
                    }
                }

                Serial.printf("✓ Found: %s at %s:%d\n",
                             printer.hostname.c_str(), printer.ip.c_str(), printer.port);
                _printers.push_back(printer);
                found++;
            }
        }

        // Progress feedback and delay every 10 IPs
        if (_scannedIPs % 10 == 0) {
            Serial.printf("Scanned %d/%d IPs (%d%%)...\n", _scannedIPs, _totalIPs, _scanProgress);
            yield();
        }
        
        // Small delay between probes to avoid overwhelming WiFi stack
        delay(10);  // Reduced from 20ms
    }

    Serial.printf("Subnet scan complete: %d IPs scanned, %d printers found\n", _scannedIPs, found);
}

bool AppKlipper::probeMoonraker(const String& ip, uint16_t port) {
    WiFiClient client;
    
    // Try TCP connection first with faster timeout
    // Reduced to 300ms for faster scanning
    if (!client.connect(ip.c_str(), port, 300)) {
        // Connection failed - port is closed or unreachable
        return false;
    }
    
    // Port is open, close the TCP probe
    client.stop();
    delay(10);  // Brief delay to let socket cleanup
    
    // Now verify it's actually Moonraker by hitting the API
    // /server/info is the correct Moonraker endpoint (no auth required)
    String url = "http://" + ip + ":" + String(port) + "/server/info";
    HTTPClient http;
    http.setTimeout(2000);  // 2 second timeout for HTTP
    
    if (!http.begin(url)) {
        return false;
    }
    
    int httpCode = http.GET();
    http.end();
    
    if (httpCode == HTTP_CODE_OK) {
        Serial.printf("✓ Moonraker found at %s:%d\n", ip.c_str(), port);
        return true;
    }
    
    return false;
}

void AppKlipper::fetchPrinterStatus(PrinterInfo& printer) {
    Serial.printf("Fetching status for %s\n", printer.hostname.c_str());

    // Query Moonraker API for printer objects
    String url = "http://" + printer.ip + ":" + String(printer.port) +
                 "/printer/objects/query?heater_bed&extruder&print_stats";

    String response = httpGet(url);
    if (response.length() == 0) {
        printer.state = "offline";
        return;
    }

    // Parse JSON response
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        printer.state = "error";
        return;
    }

    // Extract data from response
    JsonObject result = doc["result"]["status"];

    // Extruder temp
    if (result.containsKey("extruder")) {
        printer.extruderTemp = result["extruder"]["temperature"] | 0.0f;
        printer.extruderTarget = result["extruder"]["target"] | 0.0f;
    }

    // Bed temp
    if (result.containsKey("heater_bed")) {
        printer.bedTemp = result["heater_bed"]["temperature"] | 0.0f;
        printer.bedTarget = result["heater_bed"]["target"] | 0.0f;
    }

    // Print stats
    if (result.containsKey("print_stats")) {
        const char* state = result["print_stats"]["state"] | "unknown";
        printer.state = state;
        const char* filename = result["print_stats"]["filename"] | "";
        printer.filename = filename;

        // Calculate progress if printing
        if (printer.state == "printing") {
            float printDuration = result["print_stats"]["print_duration"] | 0.0f;
            float totalDuration = result["print_stats"]["total_duration"] | 0.0f;
            if (totalDuration > 0) {
                printer.progress = (printDuration / totalDuration) * 100.0f;
            }
        }
    }

    Serial.printf("Status: %s, Extruder: %.1f/%.1f, Bed: %.1f/%.1f\n",
                  printer.state.c_str(), printer.extruderTemp, printer.extruderTarget,
                  printer.bedTemp, printer.bedTarget);
}

String AppKlipper::httpGet(const String& url) {
    HTTPClient http;
    http.setTimeout(5000);  // 5 second timeout

    if (!http.begin(url)) {
        Serial.println("HTTP begin failed");
        return "";
    }

    int httpCode = http.GET();
    String payload = "";

    if (httpCode == HTTP_CODE_OK) {
        payload = http.getString();
    } else {
        Serial.printf("HTTP GET failed: %d\n", httpCode);
    }

    http.end();
    return payload;
}

void AppKlipper::drawScanning() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();

    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // Header
        display.setTextSize(3);
        display.setCursor(10, 10);
        display.print("Klipper Monitor");
        display.drawLine(0, 45, display.width(), 45, GxEPD_BLACK);

        // Scanning message
        display.setTextSize(2);
        display.setCursor(10, 80);
        if (_scanning) {
            display.print("Scanning for printers...");
            
            // Progress bar
            display.setCursor(10, 120);
            display.print("Progress: ");
            display.print(_scanProgress);
            display.print("%");
            
            // Draw progress bar
            int barWidth = display.width() - 40;
            int barHeight = 30;
            int barX = 20;
            int barY = 160;
            display.drawRect(barX, barY, barWidth, barHeight, GxEPD_BLACK);
            int fillWidth = (_scanProgress * (barWidth - 4)) / 100;
            if (fillWidth > 0) {
                display.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, GxEPD_BLACK);
            }
            
            // Status
            display.setCursor(10, 220);
            display.print("Scanned: ");
            display.print(_scannedIPs);
            display.print(" / ");
            display.print(_totalIPs);
            display.print(" IPs");
            
        } else if (_printers.empty()) {
            display.print("No Klipper printers found.");
            display.setCursor(10, 120);
            display.print("Make sure Moonraker is running");
            display.setCursor(10, 150);
            display.print("and on the same network.");
            display.setCursor(10, 200);
            display.setTextSize(3);
            display.print("Press button");
            display.setCursor(10, 240);
            display.print("to scan again");
        }

    } while (display.nextPage());
}

void AppKlipper::drawPrinterList() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();

    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // Header
        display.setTextSize(3);
        display.setCursor(10, 10);
        display.print("Klipper Printers");
        display.drawLine(0, 45, display.width(), 45, GxEPD_BLACK);

        // List printers
        int y = 60;
        int idx = 0;
        for (const auto& printer : _printers) {
            // Hostname (bold)
            display.setTextSize(2);
            display.setCursor(10, y);
            display.print("Host: ");
            display.print(printer.hostname);

            // IP and state
            display.setCursor(10, y + 25);
            display.print(printer.ip);
            display.print(" - ");
            display.print(printer.state);

            // Temps
            display.setCursor(10, y + 50);
            display.print("E:");
            display.print((int)printer.extruderTemp);
            display.print("C  B:");
            display.print((int)printer.bedTemp);
            display.print("C");

            // Separator
            display.drawLine(5, y + 75, display.width() - 5, y + 75, GxEPD_BLACK);

            y += 90;
            idx++;

            // Don't overflow screen
            if (y > display.height() - 40) break;
        }

    } while (display.nextPage());
}

void AppKlipper::drawPrinterDetails() {
    if (_printers.empty() || _selectedIndex >= (int)_printers.size()) return;

    PrinterInfo& printer = _printers[_selectedIndex];
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();

    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // Header - hostname
        display.setTextSize(3);
        display.setCursor(10, 10);
        display.print(printer.hostname);
        display.drawLine(0, 45, display.width(), 45, GxEPD_BLACK);

        // IP Address
        display.setTextSize(2);
        display.setCursor(10, 60);
        display.print("IP: ");
        display.print(printer.ip);
        display.print(":");
        display.print(printer.port);

        // Status
        display.setTextSize(3);
        display.setCursor(10, 100);
        display.print("Status: ");
        // Capitalize first letter
        String stateDisplay = printer.state;
        if (stateDisplay.length() > 0) {
            stateDisplay[0] = toupper(stateDisplay[0]);
        }
        display.print(stateDisplay);

        // Temperatures section
        display.drawLine(0, 150, display.width(), 150, GxEPD_BLACK);

        display.setTextSize(2);
        display.setCursor(10, 170);
        display.print("TEMPERATURES");

        // Extruder
        display.setTextSize(3);
        display.setCursor(10, 210);
        display.print("Extruder:");
        display.setCursor(200, 210);
        display.print((int)printer.extruderTemp);
        display.print("/");
        display.print((int)printer.extruderTarget);
        display.setTextSize(2);
        display.print(" C");

        // Bed
        display.setTextSize(3);
        display.setCursor(10, 260);
        display.print("Bed:");
        display.setCursor(200, 260);
        display.print((int)printer.bedTemp);
        display.print("/");
        display.print((int)printer.bedTarget);
        display.setTextSize(2);
        display.print(" C");

        // Print info if printing
        if (printer.state == "printing" && printer.filename.length() > 0) {
            display.drawLine(0, 320, display.width(), 320, GxEPD_BLACK);

            display.setTextSize(2);
            display.setCursor(10, 340);
            display.print("PRINT JOB");

            display.setTextSize(2);
            display.setCursor(10, 380);
            // Truncate filename if too long
            String fname = printer.filename;
            if (fname.length() > 30) {
                fname = fname.substring(0, 27) + "...";
            }
            display.print(fname);

            // Progress bar
            display.setCursor(10, 420);
            display.print("Progress: ");
            display.print((int)printer.progress);
            display.print("%");

            // Draw progress bar
            int barWidth = display.width() - 60;
            int barHeight = 25;
            int barX = 30;
            int barY = 460;
            display.drawRect(barX, barY, barWidth, barHeight, GxEPD_BLACK);
            int fillWidth = (printer.progress * (barWidth - 4)) / 100;
            if (fillWidth > 0) {
                display.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, GxEPD_BLACK);
            }
        }

        // Footer
        display.setTextSize(2);
        display.setCursor(10, display.height() - 30);
        display.print("Press to return | Next for others");

    } while (display.nextPage());
}
