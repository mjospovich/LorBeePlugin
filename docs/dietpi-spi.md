# Enable SPI on DietPi (LoRa RFM9x)

The optional **LoRa** service talks to an **RFM9x** (SX1276) module over **SPI0**. The kernel must expose **`/dev/spidev0.*`** before the container or host binary can use the bus.

This doc is for **DietPi** on Raspberry Pi (e.g. **Pi Zero 2W**). For the full edge stack and wiring, see **[lora/README.md](../lora/README.md)** and **[architecture.md](architecture.md)**.

## Enable SPI (DietPi)

1. Run **`sudo dietpi-config`**.
2. Go to **Advanced Options**.
3. Open **SPI state** (wording may be **SPI** / **SPI State** depending on DietPi version).
4. Set **SPI** to **Enable** (or **Full** / **Both** if the menu offers chip-select options — you want SPI0 available).
5. **Finish**, then **reboot** when prompted (or `sudo reboot`).

After reboot, check:

```bash
ls -l /dev/spidev0.*
```

You should see at least **`/dev/spidev0.0`** and often **`/dev/spidev0.1`**. The first-run LoRa prompts ask which index (**0** or **1**) matches how you wired the stack; see **[lora/README.md — SPI device index](../lora/README.md#spi-device-index-spidev0x)**.

## Raspberry Pi OS (not DietPi)

**`sudo raspi-config`** → **Interface Options** → **SPI** → **Yes** → reboot, then verify **`ls /dev/spidev0.*`** as above.

Equivalent config line (if you edit **`/boot/firmware/config.txt`** manually): **`dtparam=spi=on`**.

## If `/dev/spidev0.*` is still missing

On newer images the firmware config may live under **`/boot/firmware/config.txt`**. If DietPi enabled SPI but nodes never appear:

- Ensure **`dtparam=spi=on`** is present in the file the kernel actually reads (sometimes **`/boot/config.txt`** is a symlink to **`/boot/firmware/config.txt`**).
- See [DietPi issue #7904](https://github.com/MichaIng/DietPi/issues/7904) for symlink / **`config.txt`** path discussion on recent Raspberry Pi OS–based images.

## Related

- **[lora/README.md](../lora/README.md)** — wiring, CS on GPIO (not 7/8), **`RFM_SPI_CHANNEL`**, ChirpStack keys.
- **`bash scripts/setup-first-run.sh`** — optional interactive LoRa pin / SPI index setup.
