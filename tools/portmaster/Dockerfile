# Cross toolchain for the PortMaster build: Debian bullseye (glibc 2.31, older than every current
# handheld firmware) and gcc 10 for aarch64. Bullseye's security suite is retired, so only main is used.
FROM debian:bullseye
RUN sed -i '/security/d' /etc/apt/sources.list && apt-get update && \
    apt-get install -y --no-install-recommends g++-aarch64-linux-gnu cmake make ca-certificates && \
    rm -rf /var/lib/apt/lists/*
