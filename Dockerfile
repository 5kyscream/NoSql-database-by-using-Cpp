# ── Stage 1: Build MiniRed ───────────────────────────────────────────────────
FROM gcc:12 AS builder
WORKDIR /src

# Install cmake
RUN apt-get update && apt-get install -y cmake && rm -rf /var/lib/apt/lists/*

COPY . .
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --parallel $(nproc) --target minired

# ── Stage 2: Minimal runtime ──────────────────────────────────────────────────
FROM debian:12-slim AS server
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 libgcc-s1 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/minired /app/minired

RUN mkdir -p /data
VOLUME ["/data"]

EXPOSE 6379
ENTRYPOINT ["/app/minired"]
CMD ["--data-dir", "/data", "--fsync", "everysec"]
