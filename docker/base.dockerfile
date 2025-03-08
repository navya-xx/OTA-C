# Dockerfile.base
FROM python:3.10-slim

# Set non-interactive mode for apt
ENV DEBIAN_FRONTEND=noninteractive

# Update & Upgrade System
RUN apt-get update && apt-get upgrade -y

# Install system dependencies
RUN apt-get update && apt-get install -y \
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
    python3-dev \
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
LABEL maintainer="navneet.gr8@gmail.com"