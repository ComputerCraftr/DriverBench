FROM ubuntu:26.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH=/opt/pipx/bin:/root/.local/bin:${PATH}
ENV PIPX_HOME=/opt/pipx
ENV PIPX_BIN_DIR=/opt/pipx/bin

RUN apt-get update && \
    apt-get install --yes --no-install-recommends \
        ca-certificates \
        clang-22 \
        clang-format-22 \
        clang-tidy-22 \
        cmake \
        git \
        glslc \
        libdrm-dev \
        libegl1-mesa-dev \
        libgbm-dev \
        libgl1-mesa-dev \
        libvulkan-dev \
        libx11-dev \
        libxcursor-dev \
        libxext-dev \
        libxi-dev \
        libxinerama-dev \
        libxrandr-dev \
        libxrender-dev \
        ninja-build \
        pipx \
        pkg-config \
        python3 && \
    rm -rf /var/lib/apt/lists/*

COPY ci/python-tool-versions.env /tmp/python-tool-versions.env
RUN . /tmp/python-tool-versions.env && \
    pipx install "cmakelang==${DB_CMAKELANG_VERSION}" && \
    pipx install "mypy==${DB_MYPY_VERSION}" && \
    pipx install "ruff==${DB_RUFF_VERSION}" && \
    rm /tmp/python-tool-versions.env

WORKDIR /workspace
