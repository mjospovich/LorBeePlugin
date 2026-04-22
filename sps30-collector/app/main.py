#!/usr/bin/env python3
"""
SPS30 PM2.5 Collector Service
Reads from Sensirion SPS30 over UART (USB), publishes to MQTT.
Part of the LorBeePlugin edge stack — data flows through Node-RED
into the merged snapshot and optionally over LoRa.
"""
import json
import os
import logging
import time
import signal
import sys
from sensirion_shdlc_driver import ShdlcSerialPort
from sensirion_driver_adapters.shdlc_adapter.shdlc_channel import ShdlcChannel
from sensirion_uart_sps30.device import Sps30Device
from sensirion_uart_sps30.commands import OutputFormat
import paho.mqtt.client as mqtt

SERIAL_PORT = os.getenv("SPS30_SERIAL_PORT", "/dev/ttyUSB0")
MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_TOPIC = os.getenv("SPS30_MQTT_TOPIC", "sps30/raw")
INTERVAL_SEC = int(os.getenv("SPS30_INTERVAL", "30"))

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
)
logger = logging.getLogger("sps30-collector")

running = True


def us_epa_aqi_pm25(pm25_ugm3: float) -> int:
    """
    US EPA AQI from PM2.5 concentration (µg/m³), 0–500 scale.
    Uses standard breakpoint table (same family as AirNow).
    """
    c = max(0.0, float(pm25_ugm3))
    if c > 500.4:
        return 500
    # (C_low, C_high, I_low, I_high)
    bp = [
        (0.0, 12.0, 0, 50),
        (12.1, 35.4, 51, 100),
        (35.5, 55.4, 101, 150),
        (55.5, 150.4, 151, 200),
        (150.5, 250.4, 201, 300),
        (250.5, 350.4, 301, 400),
        (350.5, 500.4, 401, 500),
    ]
    for c_lo, c_hi, i_lo, i_hi in bp:
        if c_lo <= c <= c_hi:
            ip = (i_hi - i_lo) / (c_hi - c_lo) * (c - c_lo) + i_lo
            return int(round(ip))
    return 500


def on_mqtt_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        logger.info(f"Connected to MQTT broker at {MQTT_BROKER}:{MQTT_PORT}")
    else:
        logger.error(f"MQTT connect failed: {reason_code}")


def on_mqtt_disconnect(client, userdata, flags, reason_code, properties):
    logger.warning(f"MQTT disconnected: {reason_code}")


def sig_handler(signum, frame):
    global running
    running = False


def main():
    signal.signal(signal.SIGTERM, sig_handler)
    signal.signal(signal.SIGINT, sig_handler)

    mqtt_client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="sps30_collector",
    )
    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_disconnect = on_mqtt_disconnect
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()

    logger.info(f"Initializing SPS30 on {SERIAL_PORT}")
    try:
        port = ShdlcSerialPort(port=SERIAL_PORT, baudrate=115200, additional_response_time=0.02)
        port.open()
    except Exception as e:
        logger.error(f"Cannot open {SERIAL_PORT}: {e}")
        sys.exit(1)

    channel = ShdlcChannel(port)
    sensor = Sps30Device(channel)

    try:
        sensor.stop_measurement()
    except Exception:
        pass

    try:
        sn = sensor.read_serial_number()
        pt = sensor.read_product_type()
        logger.info(f"SPS30: {pt} SN={sn}")
    except Exception as e:
        logger.error(f"SPS30 init failed: {e}")
        port.close()
        sys.exit(1)

    sensor.start_measurement(OutputFormat.OUTPUT_FORMAT_FLOAT)

    max_retries = 3
    retry_delay = 1.0

    while running:
        time.sleep(INTERVAL_SEC)
        for attempt in range(max_retries):
            try:
                (
                    mc_1p0, mc_2p5, mc_4p0, mc_10p0,
                    nc_0p5, nc_1p0, nc_2p5, nc_4p0, nc_10p0,
                    typical_size,
                ) = sensor.read_measurement_values_float()
                aqi = us_epa_aqi_pm25(mc_2p5)
                payload = {
                    "pm1_0": round(mc_1p0, 3),
                    "pm2_5": round(mc_2p5, 3),
                    "pm4_0": round(mc_4p0, 3),
                    "pm10": round(mc_10p0, 3),
                    "typical_particle_size_um": round(typical_size, 3),
                    "aqi": aqi,
                }
                mqtt_client.publish(MQTT_TOPIC, json.dumps(payload), qos=0)
                logger.info(f"Published: pm2.5={mc_2p5:.2f} µg/m³ AQI={aqi}")
                break
            except Exception as e:
                if attempt < max_retries - 1:
                    logger.debug(f"Read attempt {attempt + 1}/{max_retries} failed: {e}")
                    time.sleep(retry_delay)
                else:
                    logger.warning(f"Read/publish error after {max_retries} attempts: {e}")

    sensor.stop_measurement()
    port.close()
    mqtt_client.loop_stop()
    logger.info("SPS30 collector stopped")


if __name__ == "__main__":
    main()
