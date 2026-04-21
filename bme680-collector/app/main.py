#!/usr/bin/env python3
"""
BME680 environmental sensor collector (I2C).
Publishes temperature, humidity, pressure, and gas resistance to MQTT.
Uses Pimoroni bme680 (Bosch compensation) — no BSEC binary; gas is raw resistance.
"""
from __future__ import annotations

import json
import logging
import os
import signal
import sys
import time

import bme680
import paho.mqtt.client as mqtt
import smbus2

PROTOCOL = os.getenv("BME680_PROTOCOL", "i2c").strip().lower()
MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_TOPIC = os.getenv("BME680_MQTT_TOPIC", "bme680/raw")
INTERVAL_SEC = int(os.getenv("BME680_INTERVAL", "60"))

I2C_DEV = os.getenv("BME680_I2C_DEVICE", "/dev/i2c-1")
I2C_BUS = int(os.getenv("BME680_I2C_BUS", "1"))
I2C_ADDR_RAW = os.getenv("BME680_I2C_ADDR", "0x76").strip().lower()
GAS_HEATER_TEMP = int(os.getenv("BME680_GAS_HEATER_TEMP_C", "320"))
GAS_HEATER_MS = int(os.getenv("BME680_GAS_HEATER_DURATION_MS", "150"))

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger("bme680-collector")

running = True


def _parse_i2c_addr(s: str) -> int:
    if s.startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def on_mqtt_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        logger.info("Connected to MQTT broker at %s:%s", MQTT_BROKER, MQTT_PORT)
    else:
        logger.error("MQTT connect failed: %s", reason_code)


def on_mqtt_disconnect(client, userdata, flags, reason_code, properties):
    logger.warning("MQTT disconnected: %s", reason_code)


def sig_handler(signum, frame):
    global running
    running = False


def open_sensor_i2c() -> bme680.BME680:
    addr = _parse_i2c_addr(I2C_ADDR_RAW)
    if addr not in (bme680.I2C_ADDR_PRIMARY, bme680.I2C_ADDR_SECONDARY):
        raise ValueError(
            f"BME680_I2C_ADDR must be 0x76 or 0x77 (got {I2C_ADDR_RAW})",
        )
    bus_num = I2C_BUS
    base = os.path.basename(I2C_DEV)
    if base.startswith("i2c-"):
        try:
            bus_num = int(base.split("-", 1)[1])
        except (IndexError, ValueError):
            pass
    i2c = smbus2.SMBus(bus_num)
    sensor = bme680.BME680(addr, i2c_device=i2c)
    sensor.set_gas_heater_temperature(GAS_HEATER_TEMP)
    sensor.set_gas_heater_duration(GAS_HEATER_MS)
    sensor.select_gas_heater_profile(0)
    return sensor


def main():
    if PROTOCOL != "i2c":
        logger.error(
            "BME680_PROTOCOL=%s is not supported by this image. "
            "Use I2C on the Pi (recommended with LoRa on SPI). "
            "SPI wiring would share the SPI0 bus with the RFM9x — use a separate stack or I2C.",
            PROTOCOL,
        )
        sys.exit(1)

    signal.signal(signal.SIGTERM, sig_handler)
    signal.signal(signal.SIGINT, sig_handler)

    mqtt_client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="bme680_collector",
    )
    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_disconnect = on_mqtt_disconnect
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()

    logger.info(
        "Initializing BME680 I2C (device %s, addr %s, bus hint %s)",
        I2C_DEV,
        I2C_ADDR_RAW,
        I2C_BUS,
    )
    try:
        sensor = open_sensor_i2c()
    except Exception as e:
        logger.error("BME680 init failed: %s", e)
        sys.exit(1)

    logger.info(
        "BME680 ready (chip_id=0x%02x); publishing to %s every %ss",
        sensor.chip_id,
        MQTT_TOPIC,
        INTERVAL_SEC,
    )

    while running:
        time.sleep(INTERVAL_SEC)
        try:
            if not sensor.get_sensor_data():
                logger.warning("No new sensor data (stale)")
                continue
            d = sensor.data
            payload = {
                "temperature_c": round(d.temperature, 2),
                "humidity_pct": round(d.humidity, 2),
                "pressure_hpa": round(d.pressure, 2),
                "gas_resistance_ohm": int(d.gas_resistance),
                "heat_stable": bool(d.heat_stable),
            }
            mqtt_client.publish(MQTT_TOPIC, json.dumps(payload), qos=0)
            logger.info(
                "Published: T=%.2f°C RH=%.1f%% P=%.1f hPa gas=%s Ω stable=%s",
                d.temperature,
                d.humidity,
                d.pressure,
                f"{d.gas_resistance:.0f}",
                d.heat_stable,
            )
        except Exception as e:
            logger.warning("Read/publish error: %s", e)

    mqtt_client.loop_stop()
    logger.info("BME680 collector stopped")


if __name__ == "__main__":
    main()
