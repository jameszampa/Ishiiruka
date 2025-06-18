FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y software-properties-common && \
    add-apt-repository universe && \
    apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    pkg-config \
    libavcodec-dev \
    libavformat-dev \
    libswscale-dev \
    libavutil-dev \
    ffmpeg \
    curl \
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libevdev-dev \
    libudev-dev \
    libgtk-3-dev \
    libfreetype-dev \
    libhidapi-dev \
    libusb-1.0-0-dev \
    libcurl4-openssl-dev \
    libsfml-dev \
    libxxf86vm-dev \
    libxinerama-dev \
    libsoup2.4-dev \
    libpulse-dev \
    pulseaudio \
    libjpeg-dev \
    libtiff-dev \
    libpng-dev \
    libexpat1-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer1.0-dev \
    libwxgtk3.0-gtk3-dev \
    libwxgtk3.0-gtk3-0v5 \
    libgtk2.0-dev \
    libasound2-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Rust and initialize environment
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y && \
    . /root/.cargo/env && \
    rustup default stable && \
    rustup update && \
    rustup component add rust-src && \
    rustup show

# Ensure Rust is in PATH for all subsequent commands
ENV PATH="/root/.cargo/bin:${PATH}"
ENV RUSTUP_HOME="/root/.rustup"
ENV CARGO_HOME="/root/.cargo"
ENV RUSTC="/root/.cargo/bin/rustc"
ENV RUSTUP_TOOLCHAIN="stable-x86_64-unknown-linux-gnu"

WORKDIR /opt

RUN git clone https://github.com/jameszampa/Ishiiruka.git && \
    cd Ishiiruka && \
    git checkout feature/headless-playback-frame-dumping && \
    git submodule update --init --recursive && \
    chmod +x build-linux-headless.sh

# Verify Rust installation and build
RUN cd Ishiiruka && \
    . /root/.cargo/env && \
    rustc --version && \
    cargo --version && \
    cmake -DRust_COMPILER=/root/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc -B build && \
    ./build-linux-headless.sh playback

COPY start.sh /opt/start.sh
RUN chmod +x /opt/start.sh

ENTRYPOINT ["/opt/start.sh"]