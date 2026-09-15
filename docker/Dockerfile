ARG UBUNTU_IMAGE=ubuntu:22.04
FROM ${UBUNTU_IMAGE}

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       build-essential cmake ninja-build procps \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --create-home --uid 1000 --shell /bin/bash developer \
    && mkdir -p /build /src \
    && chown developer:developer /build

USER developer
WORKDIR /build
CMD ["/bin/bash"]
