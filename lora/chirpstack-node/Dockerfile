# LoRaWAN OTAA node (RadioLib + PiHal/lgpio). Build on the Pi (or matching arch).
# Requires SPI enabled on the host and RFM9x wired per lora/README.md.

FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates libyaml-cpp-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/joan2937/lg.git /tmp/lg \
    && make -j2 -C /tmp/lg \
    && make -C /tmp/lg install \
    && ldconfig

ARG RADIOLIB_VERSION=7.6.0
RUN git clone --depth 1 --branch ${RADIOLIB_VERSION} https://github.com/jgromes/RadioLib.git /opt/RadioLib

WORKDIR /opt/chirpstack-node
COPY CMakeLists.txt main.cpp config.hpp config_yaml.cpp snapshot.hpp snapshot.cpp \
    payload_builder.hpp payload_builder.cpp lora_state.hpp lora_state.cpp config.yaml ./
RUN mkdir -p /etc/lora && cp config.yaml /etc/lora/config.yaml
RUN mkdir build && cd build \
    && cmake .. -DRADIOLIB_ROOT=/opt/RadioLib \
    && cmake --build . -j2

FROM debian:bookworm-slim

# Must match the soname linked by the builder (bookworm: libyaml-cpp0.7, not 0.8).
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libyaml-cpp0.7 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/liblgpio.so* /usr/local/lib/
COPY --from=builder /opt/chirpstack-node/build/lorawan-node /usr/local/bin/
COPY --from=builder /etc/lora/config.yaml /etc/lora/config.yaml
RUN ldconfig

ENV LORA_CONFIG=/etc/lora/config.yaml

ENTRYPOINT ["/usr/local/bin/lorawan-node"]
