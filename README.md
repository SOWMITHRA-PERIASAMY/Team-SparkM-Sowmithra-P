LAB ASSET CHECKOUT BEACON — PROJECT DESCRIPTION AND HOW TO RUN IT

PROJECT DESCRIPTION

The Problem
Shared lab tools, kits, meters, and equipment are frequently borrowed without any record of who took them, when, or when they're due back. Manual logbooks are slow, easy to skip, and give zero real-time visibility into what's currently checked out or overdue. This leads to lost equipment, indefinite informal borrowing, and no way for lab administrators to trace an item's custody history.

The Solution
A physical checkout station built around an ESP32 and an RFID reader. Users tap their ID card, then tap the asset's tag. This two-tap flow instantly logs who took what, with immediate LCD, LED, and buzzer feedback. Every transaction is written to the device's own flash storage first, so the system keeps working even with no network connection, then syncs to a cloud backend whenever WiFi becomes available.

Key Features
Two-tap identity model, proving who took an item, not just what was taken. Local-first design, where every transaction saves to flash before any network call is attempted, so the device is fully functional offline. Real checkout, return, and conflict logic that distinguishes a fresh checkout, a same-user return, and someone trying to take an already-held item. Tamper detection through a microswitch on the enclosure lid that logs unauthorized openings as an independent, timestamped event. Usage analytics per asset that track checkout frequency, laying the groundwork for predictive maintenance insights. WiFi sync with a queue, so transactions sync to the backend when online without ever blocking the physical checkout flow. A manager dashboard showing live asset status, transaction log, usage frequency, tamper events, and a downloadable PDF report.

System Architecture
The RFID reader, LCD, RTC, LEDs, buzzer, and tamper switch all connect to the ESP32, which handles all logic locally. The ESP32 connects over WiFi to a FastAPI and SQLite backend, which in turn feeds the manager dashboard. All checkout, return, and conflict decisions are made locally on the ESP32. The backend and dashboard exist for visibility and reporting, not for the core logic to function.

Hardware Components
ESP32 DevKit as the main controller. MFRC522 RFID reader to detect user ID cards and asset tags. A 16x2 LCD with I2C backpack for live status messages. A DS3231 RTC module for accurate timestamps that survive power loss thanks to its battery. Three LEDs, red, amber, and green, for visual feedback. A buzzer for audio confirmation on every scan. A tamper switch to detect unauthorized enclosure opening.
