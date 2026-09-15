# syntax=docker/dockerfile:1.4
FROM xiaoai-plus-toolchain:dev

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      alsa-utils:armhf \
      qemu-user-static \
    && rm -rf /var/lib/apt/lists/*

COPY runtime-test/asound.conf /etc/asound.conf
COPY runtime-test/arecord-wrapper.sh /usr/local/bin/arecord
COPY runtime-test/aplay-wrapper.sh /usr/local/bin/aplay

RUN chmod +x /usr/local/bin/arecord /usr/local/bin/aplay

WORKDIR /workspace
CMD ["/bin/bash"]
