# 📷 ESP32-CAM Smart Security System

A smart security and surveillance system built using the **ESP32-CAM**. The system uses the camera to monitor the surroundings, detect motion, capture images, and send security alerts remotely through Telegram.

## 🚀 Features

* 📷 ESP32-CAM based live camera monitoring
* 🚨 Motion detection using a PIR sensor
* 📸 Automatic image capture when motion is detected
* 📩 Telegram notification with captured images
* 🌐 Remote camera streaming
* ☁️ Cloud relay support for remote access
* 🔌 Can operate independently using a power source

## 🛠️ Hardware Used

* ESP32-CAM (AI Thinker)
* PIR Motion Sensor
* USB / 5V Power Source or Power Bank
* Jumper Wires

## 💻 Software & Technologies

* Arduino IDE
* ESP32 Arduino Core
* ESP32-CAM Camera Library
* Telegram Bot API
* Wi-Fi
* WebSocket-based communication

## ⚙️ How It Works

1. The ESP32-CAM connects to the configured Wi-Fi network.
2. The camera initializes and starts the surveillance system.
3. The PIR sensor continuously monitors for movement.
4. When motion is detected, the ESP32-CAM captures an image.
5. The captured image is sent to the configured Telegram bot.
6. The camera stream can also be accessed remotely through the configured streaming/relay system.

## 📁 Repository Contents

```text
ESP32-CAM-Security-System/
│
├── ESP32_CAM_Security.ino
└── README.md
```

## 🔧 Setup

1. Install **Arduino IDE**.
2. Install the required ESP32 board package and libraries.
3. Open the `.ino` file in Arduino IDE.
4. Enter your Wi-Fi and Telegram configuration details.
5. Select the appropriate ESP32-CAM board.
6. Connect the ESP32-CAM to your computer.
7. Compile and upload the program.
8. Power the ESP32-CAM and test motion detection and Telegram alerts.

## 🔐 Configuration

Before uploading the code, configure the required credentials such as:

* Wi-Fi SSID
* Wi-Fi password
* Telegram Bot Token
* Telegram Chat ID
* Streaming/relay configuration, if enabled

**Do not upload real passwords, bot tokens, API keys, or other private credentials to GitHub.**

## 🎯 Project Objective

The objective of this project is to develop a compact and low-cost IoT-based security system capable of detecting movement, capturing images, and providing remote security notifications.

## 👨‍💻 Project

**ESP32-CAM Smart Security & Surveillance System**

Built using ESP32-CAM and Arduino.
# ESP32-CAM-Security-System
ESP32-CAM based smart security system with motion detection, image capture, and Telegram alerts. The project uses an ESP32-CAM to detect motion and capture images for remote security monitoring.
