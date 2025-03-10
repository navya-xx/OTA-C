# Dockerfile.base
FROM ubuntu:22.04

# Set non-interactive mode for apt
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update

RUN apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ccache \
    cmake \
    cpufrequtils \
    ethtool \
    g++ \
    git \
    inetutils-tools \
    libboost-all-dev \
    libncurses5 \
    libncurses5-dev \
    libusb-1.0-0 \
    libusb-1.0-0-dev \
    libusb-dev \
    python3.10 \
    python3.10-dev \
    python3-pip \
    python3-setuptools \
    wget \
    curl \
    swig \
    pkg-config \
    libssl-dev \
    libwebsockets-dev \
    doxygen \
    && rm -rf /var/lib/apt/lists/*

# Install Python dependencies
RUN pip install --upgrade pip && pip install --no-cache-dir mako requests numpy scipy ruamel.yaml

# Tag a label for clarity (optional)
LABEL maintainer="Navneet Agrawal"