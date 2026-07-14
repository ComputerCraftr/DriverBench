FROM ghcr.io/void-linux/void-glibc-full@sha256:dc575a91e922684044c39224cd477833b320a70b22ca2ed84c7e6f4ea7e18382

ENV PATH=/usr/lib/llvm/22/bin:${PATH}

RUN xbps-install -Suy xbps && \
    xbps-install -y \
        binutils \
        ca-certificates \
        clang22 \
        cmake \
        compiler-rt22 \
        git \
        ninja \
        python3 && \
    xbps-remove -O

WORKDIR /workspace
