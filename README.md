Lab Asset Checkout Beacon
An RFID-based accountability system for shared lab equipment.


Project Description
The Problem
Shared lab tools, kits, meters, and equipment are frequently borrowed without any record of who took them, when, or when they're due back. Manual logbooks are slow, easy to skip, and give zero real-time visibility into what's currently checked out or overdue. This leads to lost equipment, indefinite informal borrowing, and no way for lab administrators to trace an item's custody history.
The Solution
A physical checkout station built around an ESP32 and an RFID reader. Users tap their ID card, then tap the asset's tag — a two-tap flow that instantly logs who took what, with immediate LCD, LED, and buzzer feedback. Every transaction is written to the device's own flash storage first, so the system keeps working even with no network connection, then syncs to a cloud backend whenever WiFi becomes available.
Key Features
Two-tap identity model — proves who took an item, not just what was taken
Local-first design — every transaction saves to flash before any network call is attempted, so the device is fully functional offline
Real checkout/return/conflict logic — distinguishes a fresh checkout, a same-user return, and someone trying to take an already-held item
Tamper detection — a microswitch on the enclosure lid logs unauthorized openings as an independent, timestamped event
Usage analytics per asset — tracks checkout frequency, laying the groundwork for predictive-maintenance-style insights
WiFi sync with a queue — transactions sync to the backend when online, without ever blocking the physical checkout flow
Manager dashboard — live asset status, transaction log, usage frequency, tamper events, and a downloadable PDF report
System Architecture
RFID Reader (SPI)  ─┐

LCD + RTC (I2C)     ─┼─→  ESP32 (edge logic)  ─→  WiFi  ─→  FastAPI + SQLite backend  ─→  Dashboard

LEDs + Buzzer       ─┤

Tamper Switch       ─┘

The ESP32 makes all checkout/return/conflict decisions locally — the backend and dashboard are for visibility and reporting, not for the core logic to function.
Hardware Components
Component
Role
ESP32 DevKit
Main controller
MFRC522 RFID Reader
Detects user ID cards and asset tags
16x2 LCD (I2C)
Shows live status messages
DS3231 RTC
Provides accurate timestamps, survives power loss via battery
3x LEDs (Red/Amber/Green)
Visual feedback — conflict / return / success
Buzzer
Audio confirmation on every scan
Tamper Switch
Detects unauthorized enclosure opening

How to Run It
1. Hardware Setup
Wire all components per the table above. Test each module individually before combining:

ESP32 alone — confirm a basic blink sketch uploads and runs
RFID reader alone — confirm a UID prints in Serial Monitor when a tag is tapped
LCD + RTC together — run an I2C scanner sketch, confirm both addresses appear (typically 0x27 for LCD, 0x68 for RTC)
LEDs + buzzer + tamper switch — confirm each responds individually
Combine everything only once every module works on its own
2. Firmware Setup
Open the .ino file in Arduino IDE. Install these libraries via Tools → Manage Libraries:

MFRC522
LiquidCrystal_I2C
RTClib
ArduinoJson
Firebase Arduino Client Library for ESP8266 and ESP32 (only if using Firebase instead of the FastAPI backend)

Fill in these values near the top of the file:

#define WIFI_SSID     "YOUR_WIFI_NAME"

#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define BACKEND_URL   "http://YOUR_LAPTOP_IP:8000/api/transactions"

Important: the WiFi network must be one your laptop (running the backend) is also reachable on. If sharing a laptop's Mobile Hotspot, use the IP shown for that hotspot's network adapter (commonly 192.168.137.1), not the laptop's regular WiFi/Ethernet IP.

Select Tools → Board → ESP32 Dev Module, choose the correct COM port, and upload.
3. Backend Setup
Install dependencies once:

pip install fastapi uvicorn --break-system-packages

Run the server, making sure to bind to 0.0.0.0 (not 127.0.0.1) so other devices on the network can reach it:

python -m uvicorn beacon_backend_main:app --host 0.0.0.0 --port 8000

Confirm it's running by opening http://localhost:8000 in a browser — you should see a JSON message confirming the API is up.
4. Dashboard Setup
Open beacon_dashboard.html in any browser. At the top, enter your backend's address (e.g., http://192.168.137.1:8000) and click Connect.

Before relying on it, update the ASSET_NAMES object near the top of the dashboard's script with your actual RFID tag UIDs, mapped to real tool names.




5. Full Test Sequence
Power on the ESP32 — LCD should show "Tap your ID"
Check Serial Monitor for "WiFi connected: [an IP address]"
Tap a user card — LCD changes to "Hi! Now tap the item"
Tap an asset tag — LCD shows "Checked out!", green LED lights, short beep
Within a few seconds, Serial Monitor should show the transaction syncing successfully
Refresh the dashboard — the asset should now show as held by that user
Tap the same two tags again — LCD shows "Returned!", amber LED, dashboard updates again
Tap a different user, then the same held asset — LCD shows "Already taken by...", red LED, longer beep
Open the enclosure lid — a tamper event should appear on the dashboard's Tamper Events tab



Troubleshooting Quick Reference
Symptom
Likely Cause
LCD stuck on "Connecting WiFi"
Wrong SSID/password, or network is 5GHz-only (ESP32 needs 2.4GHz)
Sync failed (code -1)
ESP32 can't reach the backend — check BACKEND_URL matches the right IP, backend is running with --host 0.0.0.0, and firewall isn't blocking the port
Reader not detecting tags
Check wiring, especially RST and SDA/SS; confirm VCC is on 3.3V not 5V; pull tags fully away between taps
Same tag won't re-read
Confirm both rfid.PICC_HaltA() and rfid.PCD_StopCrypto1() are called after each read
Buzzer silent, LEDs fine
Try swapping the buzzer's two leads — some are polarity-sensitive
Dashboard shows "Unregistered Tool"
Add that tag's UID to the ASSET_NAMES object in the dashboard script


