# LoRaWAN OTAA node (RadioLib + PiHal/lgpio). Build on the Pi (or matching arch).
# Requires SPI enabled on the host and RFM9x wired per lora/README.md.

FROM debian:bookworm-slim AS builder

# RadioLib is a large C++ compile; -j2 often triggers OOM (Docker exit 137) on 512MB–1GB Pis.
# Override on a bigger host: docker compose build --build-arg BUILD_JOBS=4
ARG BUILD_JOBS=1

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates libyaml-cpp-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/joan2937/lg.git /tmp/lg \
    && make -j${BUILD_JOBS} -C /tmp/lg \
    && make -C /tmp/lg install \
    && ldconfig

ARG RADIOLIB_VERSION=7.6.0
RUN git clone --depth 1 --branch ${RADIOLIB_VERSION} https://github.com/jgromes/RadioLib.git /opt/RadioLib

WORKDIR /opt/chirpstack-node

# --- Dependency layer (cached until CMakeLists.txt or RadioLib version changes) ---
# Stub source files let cmake configure + compile RadioLib and fetch nlohmann_json.
# Our stubs compile to empty .o; the final link fails (no main) but deps are built.
COPY CMakeLists.txt ./
RUN touch main.cpp config.hpp config_yaml.cpp snapshot.hpp snapshot.cpp \
    payload_builder.hpp payload_builder.cpp lora_state.hpp lora_state.cpp \
    downlink_handler.hpp downlink_handler.cpp config.yaml \
    && mkdir build && cd build \
    && cmake .. -DRADIOLIB_ROOT=/opt/RadioLib \
    && cmake --build . -j${BUILD_JOBS} || true

# --- Application layer (only this rebuilds on source changes — seconds, not minutes) ---
COPY main.cpp config.hpp config_yaml.cpp snapshot.hpp snapshot.cpp \
    payload_builder.hpp payload_builder.cpp lora_state.hpp lora_state.cpp \
    downlink_handler.hpp downlink_handler.cpp config.yaml ./
RUN touch main.cpp config.hpp config_yaml.cpp snapshot.hpp snapshot.cpp \
    payload_builder.hpp payload_builder.cpp lora_state.hpp lora_state.cpp \
    downlink_handler.hpp downlink_handler.cpp
RUN mkdir -p /etc/lora && cp config.yaml /etc/lora/config.yaml
RUN cd build && cmake --build . -j${BUILD_JOBS}

FROM debian:bookworm-slim

# Must match the soname linked by the builder (bookworm: libyaml-cpp0.7, not 0.8).
# mosquitto-clients: downlink commands publish to local Mosquitto via mosquitto_pub.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libyaml-cpp0.7 mosquitto-clients \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/liblgpio.so* /usr/local/lib/
COPY --from=builder /opt/chirpstack-node/build/lorawan-node /usr/local/bin/
COPY --from=builder /etc/lora/config.yaml /etc/lora/config.yaml
RUN ldconfig

ENV LORA_CONFIG=/etc/lora/config.yaml

ENTRYPOINT ["/usr/local/bin/lorawan-node"]
