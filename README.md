# Stairwell Effects

This project implements an ESPHome-based smart lighting system for a stairwell, utilizing an ESP8266 (ESP-01 1M) to control WS2811 LED strips with motion-triggered effects. The system responds to PIR sensors to create dynamic lighting sequences, including fade effects, group lighting, and special "Easter egg" modes like Disco, Fire Flicker, and Moonlight. It integrates with Home Assistant for monitoring and control.

## Features

- **Motion-Triggered Lighting**: Two PIR sensors (upper and lower) detect movement to trigger LED sequences for ascending or descending the stairs.
- **Dynamic LED Effects**: Includes fade-in/fade-out, group lighting, and special effects like:
  - **Disco Mode**: Colorful rainbow pattern triggered by activating both PIRs three times within 5 seconds.
  - **Fire Flicker**: Warm, candle-like glow triggered by five rapid lower PIR activations within 3 seconds.
  - **Moonlight**: Pulsing blue effect activated by holding both PIRs for 10 seconds.
  - Other effects include Breathing Trail, Step Highlight, and Wave Runner.
- **Environmental Monitoring**: DHT11 sensor for temperature and humidity, and integration with a Home Assistant illuminance sensor.
- **Configurable Parameters**: Adjustable LED group size via Home Assistant.
- **Time-Based Automation**: Uses SNTP for time synchronization and sun position for conditional lighting.
- **Storage Lighting**: Monochromatic LED strip controlled via a MOSFET for storage area illumination.
- **OTA Updates and Web Server**: Supports over-the-air updates and a web interface for configuration.
- **Fallback Wi-Fi**: Captive portal for setup if Wi-Fi connection fails.

## Hardware Requirements

- **Microcontroller**: ESP8266 (ESP-01 1M)
- **LED Strip**: WS2811 (58 LEDs for stairs, BRG color order)
- **PIR Sensors**: Two GPIO-connected PIR sensors (pins 12 and 14)
- **DHT11 Sensor**: For temperature and humidity (GPIO0)
- **MOSFET**: For controlling a 12V monochromatic LED strip (GPIO5)
- **Power Supply**: 12V for LED strip, 3.3V for ESP8266
- **Wi-Fi Network**: For Home Assistant integration and OTA updates

## Configuration

The `esphome.yaml` file includes:
- **Wi-Fi**: Configures connection with a fallback hotspot.
- **API**: Encrypted communication with Home Assistant.
- **OTA**: Secure over-the-air updates.
- **Sensors**: DHT11 for temperature/humidity, Home Assistant illuminance sensor integration.
- **Binary Sensors**: PIR sensors with debouncing for reliable motion detection.
- **Lights**: WS2811 LED strip with multiple effects and a monochromatic storage light.
- **Globals**: Variables to track state, direction, and effect progress.
- **Time and Sun**: SNTP time synchronization and sun position for conditional logic.

## Usage

- **Normal Operation**: Walk up or down the stairs to trigger the upper or lower PIR sensor, activating fade or group lighting effects.
- **Easter Eggs**:
  - **Disco Mode**: Trigger both PIRs three times within 5 seconds for a 10-second rainbow effect.
  - **Fire Flicker**: Rapidly trigger the lower PIR five times within 3 seconds for a 15-second flickering glow.
  - **Moonlight**: Hold both PIRs active for 10 seconds for a 30-second blue pulsing effect.
- **Adjust Group Size**: Use the `LED Group Size` number entity in Home Assistant to change the number of LEDs lit per group.
- **Monitor Environment**: Check temperature, humidity, and ambient light status via Home Assistant.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
