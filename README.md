# AC High Low Protecting System Using ESP32 with Webpage

> A smart AC voltage protection system built on ESP32 that monitors mains voltage in real time, automatically disconnects the load during over-voltage or under-voltage conditions, and exposes a beautiful live web dashboard over WiFi.

---

## Dashboard Preview

The built-in web interface runs entirely on the ESP32 — no cloud, no app required.  
Open any browser on the same WiFi network and navigate to the device IP address.

![Web Dashboard](images/web_dashboard.png)

---

## Key Features

- **Automatic Over-Voltage Protection** — disconnects load when voltage exceeds threshold (default: 240 V)  
- **Automatic Under-Voltage Protection** — disconnects load when voltage drops below threshold (default: 180 V)  
- **Dual Relay Output** — protect two independent loads simultaneously  
- **3-Color LED Indicator** — Red (over), Yellow (under), Green (normal)  
- **Audible Buzzer Alert** — with mute option via the web UI  
- **16×2 I²C LCD Display** — shows live voltage and status without WiFi  
- **Persistent Settings** — thresholds saved to ESP32 flash (NVS), survive reboot  
- **Responsive Web Dashboard** — dark-mode, mobile-friendly, no external server needed  
- **Test Lab (Simulation Mode)** — inject voltages, run wave patterns, one-shot sequences from the browser
- **Demo Mode** — automated hardware cycle (all LEDs + relays + buzzer)

---

## System Architecture

![System Architecture](images/system_flowchart.png)

---

## Hardware Required

| Component | Qty | Notes |
|---|---|---|
| ESP32 Dev Board | 1 | Any 30-pin or 38-pin variant |
| ZMPT101B Voltage Sensor Module | 1 | Calibrated for AC mains |
| 5V 2-Channel Relay Module | 1 | Active-LOW trigger |
| 16×2 I²C LCD (0x27) | 1 | HD44780 + PCF8574 backpack |
| Red LED | 1 | Over-voltage indicator |
| Yellow LED | 1 | Under-voltage indicator |
| Green LED | 1 | Normal / stable indicator |
| Active Buzzer Module | 1 | Or passive buzzer with transistor driver |
| BC547 NPN Transistors | 4 | Driver for LEDs and buzzer |
| 220 Ω Resistors | 3 | LED current limiting |
| 1 kΩ Resistors | 4 | Transistor base resistors |
| 10 kΩ Resistor | 1 | ADC input bias for ZMPT101B |
| 100 Ω Resistor | 1 | ADC input series resistor |

---

## Pin Connections

```
ESP32 GPIO  →  Component
────────────────────────────────
GPIO 21     →  LCD SDA
GPIO 22     →  LCD SCL
GPIO 34     →  ZMPT101B OUT  (via 100 Ω series + 10 kΩ to GND)
GPIO 27     →  Relay Module IN1
GPIO 26     →  Relay Module IN2
GPIO 25     →  BC547 Base (1 kΩ) → Buzzer collector
GPIO 13     →  BC547 Base (1 kΩ) → Red LED (220 Ω) collector
GPIO 12     →  BC547 Base (1 kΩ) → Yellow LED (220 Ω) collector
GPIO 14     →  BC547 Base (1 kΩ) → Green LED (220 Ω) collector
3.3 V       →  ZMPT101B VCC, LCD VCC (via I²C backpack)
5 V         →  Relay VCC
GND         →  Common GND
```

---

## Circuit Diagram

![Circuit Diagram](images/circuit_diagram.png)

---

## Software Setup

### Prerequisites

- [Arduino IDE 2.x](https://www.arduino.cc/en/software) or [PlatformIO](https://platformio.org/)
- ESP32 Board Support Package installed  
  *(File → Preferences → Board Manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`)*

### Required Libraries

Install all via **Sketch → Include Library → Manage Libraries**:

| Library | Author |
|---|---|
| `EmonLib` | OpenEnergyMonitor |
| `LiquidCrystal I2C` | Frank de Brabander |

`Wire.h`, `WiFi.h`, `WebServer.h`, and `Preferences.h` are all included with the ESP32 core.

### Configuration

Open `smart_helmet.ino` and edit the WiFi credentials:

```cpp
const char* ssid     = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
```

### Upload

1. Connect your ESP32 via USB
2. Select the correct board and port in Arduino IDE
3. Click **Upload**
4. Open **Serial Monitor** at 115200 baud
5. Note the IP address printed
6. Navigate to that IP in any browser on the same WiFi network

---

## Web API Endpoints

| Endpoint | Description |
|---|---|
| `/` | Main web dashboard |
| `/livedata` | JSON: voltage, status, relay states, alerts |
| `/histdata` | JSON: last 60 voltage readings for the chart |
| `/r1on` `/r1off` `/r1auto` | Relay 1 manual control |
| `/r2on` `/r2off` `/r2auto` | Relay 2 manual control |
| `/bothon` `/bothoff` `/bothauto` | Both relays at once |
| `/buzzeron` `/buzzeroff` `/buzzermute` | Buzzer control |
| `/savesettings?under=180&over=240` | Save threshold settings |
| `/simon?v=220` | Simulation mode — inject voltage |
| `/simoff` | Return to live sensor |
| `/demoon` `/demooff` `/demostep` | Demo mode control |
| `/resetstats` `/resetpeak` | Reset counters / peak values |

---

## How It Works

```
ZMPT101B → ADC GPIO34 → EmonLib (calcVI) → Filtered Voltage
                                              │
                            ┌─────────────────┤
                            │                 │
                         Normal              Fault (over/under)
                            │                 │
                      Green LED ON          Relay trips (load off)
                      Relays stay ON        Red or Yellow LED ON
                                            Buzzer sounds
                                            Alert logged
```

The ESP32 main loop reads voltage every second, applies a 70/30 low-pass filter for stability, and updates the LCD, LEDs, relays, and buzzer accordingly.

---

## Safety Warning

> **This project deals with mains AC voltage (110 V / 220 V), which is lethal.**  
> Ensure all high-voltage wiring is done by a qualified person.  
> Use appropriate enclosures, insulation, and circuit breakers.  
> The sensor module (ZMPT101B) provides isolation from mains — do not bypass it.  
> This project is for educational and hobbyist use only.

---

## Project Structure

```
smart_helmet/
├── smart_helmet.ino     # Main Arduino sketch (ESP32)
├── README.md            # This file
├── LICENSE              # MIT License
├── .gitignore           # Arduino / build artifacts
└── images/
    ├── circuit_diagram.png   # Hardware connection diagram
    ├── system_flowchart.png  # Operation flowchart
    └── web_dashboard.png     # Dashboard screenshot
```

---

## Roadmap

- [ ] Add OTA (Over-The-Air) firmware updates
- [ ] Email/Telegram alert on fault condition
- [ ] Data logging to SD card or Google Sheets
- [ ] MQTT integration (Home Assistant / Node-RED)

---

## License

This project is released under the [MIT License](LICENSE).

---

## Author

**Sam Joseph**  

---

*Built for real-world AC protection. Open source. No cloud required.*
