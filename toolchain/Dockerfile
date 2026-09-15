# syntax=docker/dockerfile:1.4
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture armhf \
    && rm -f /etc/apt/sources.list.d/ubuntu.sources \
    && printf '%s\n' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble main restricted universe multiverse' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-updates main restricted universe multiverse' \
      'deb [arch=amd64] http://security.ubuntu.com/ubuntu noble-security main restricted universe multiverse' \
      > /etc/apt/sources.list \
    && printf '%s\n' \
      'deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse' \
      'deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse' \
      'deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse' \
      > /etc/apt/sources.list.d/armhf.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      ca-certificates curl git \
      build-essential cmake ninja-build pkg-config python3 protobuf-compiler \
      squashfs-tools \
      crossbuild-essential-armhf \
      libeigen3-dev \
      libssl-dev:armhf zlib1g-dev:armhf \
      libwebrtc-audio-processing-dev:armhf \
    && rm -rf /var/lib/apt/lists/*

COPY root-oh2p.squashfs /tmp/root-oh2p.squashfs
RUN mkdir -p /opt/sysroot \
    && unsquashfs -f -d /opt/sysroot /tmp/root-oh2p.squashfs \
    && mkdir -p /opt/sysroot/usr/include /opt/sysroot/usr/lib \
    && cp -a /usr/include/openssl /opt/sysroot/usr/include/ 2>/dev/null || true \
    && cp -a /usr/include/arm-linux-gnueabihf/openssl /opt/sysroot/usr/include/ 2>/dev/null || true \
    && cp -a /usr/include/zlib.h /usr/include/zconf.h /opt/sysroot/usr/include/ 2>/dev/null || true \
    && cp -a /usr/include/webrtc_audio_processing /opt/sysroot/usr/include/ 2>/dev/null || true \
    && cp -a /usr/lib/arm-linux-gnueabihf/libz.a /opt/sysroot/usr/lib/ \
    && cp -a /usr/lib/arm-linux-gnueabihf/libssl.a /opt/sysroot/usr/lib/ \
    && cp -a /usr/lib/arm-linux-gnueabihf/libcrypto.a /opt/sysroot/usr/lib/ \
    && cp -a /usr/lib/arm-linux-gnueabihf/libwebrtc_audio_processing.a /opt/sysroot/usr/lib/ \
    && rm -f /tmp/root-oh2p.squashfs

ENV PKG_CONFIG_ALLOW_CROSS=1
ENV PKG_CONFIG_SYSROOT_DIR=/opt/sysroot
ENV PKG_CONFIG_LIBDIR=/opt/sysroot/usr/lib/pkgconfig:/opt/sysroot/lib/pkgconfig

WORKDIR /workspace
CMD ["/bin/bash"]
