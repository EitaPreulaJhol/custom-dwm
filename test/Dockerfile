FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential pkg-config \
      libx11-dev libxext-dev libxft-dev libxinerama-dev libfontconfig1-dev \
      xvfb x11-utils x11-xserver-utils x11-apps procps xdotool imagemagick fonts-dejavu-core \
 && rm -rf /var/lib/apt/lists/*

# Optional: Nerd Font icons for the taskbar. The host font is mounted by the
# run script; see test/run.sh.
WORKDIR /src
