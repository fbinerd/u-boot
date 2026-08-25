# syntax=docker/dockerfile:1.7
FROM debian@sha256:abd67ffcfa541b485a3dff59865ab629aa048a6c613e639d36e7456b0b229241

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install --yes --no-install-recommends \
      bc \
      bison \
      build-essential \
      ca-certificates \
      device-tree-compiler \
      flex \
      gcc-aarch64-linux-gnu \
      git \
      libgnutls28-dev \
      libssl-dev \
      python3 \
      python3-dev \
      python3-pyelftools \
      python3-setuptools \
      swig \
 && rm -rf /var/lib/apt/lists/*

ENV ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu-

WORKDIR /workspace/u-boot

CMD ["bash"]
