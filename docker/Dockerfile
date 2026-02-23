FROM nvidia/cuda:13.1.1-cudnn-devel-ubuntu24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    curl \
    ca-certificates \
    libvulkan1 \
    libvulkan-dev \
    vulkan-tools \
    vulkan-validationlayers \
    mesa-vulkan-drivers \
    glslang-tools \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends ninja-build \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
COPY . .

# ---- Slang ----
ARG SLANG_VERSION=2026.3.1
RUN set -eux; \
    mkdir -p /opt/slang; \
    curl -fsSL -o /tmp/slang.tar.gz \
      "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-linux-x86_64.tar.gz"; \
    tar -xzf /tmp/slang.tar.gz -C /opt/slang; \
    rm -f /tmp/slang.tar.gz; \
    echo "== slang extract tree (top) =="; \
    find /opt/slang -maxdepth 3 -type d -print; \
    echo "== searching for slangc =="; \
    SLANGC_PATH="$(find /opt/slang -type f -name slangc -perm -111 | head -n 1)"; \
    test -n "$SLANGC_PATH"; \
    echo "Found slangc at: $SLANGC_PATH"; \
    install -m 0755 "$SLANGC_PATH" /usr/local/bin/slangc; \
    echo "== searching for libslang-compiler =="; \
    SLANG_LIB_DIR="$(dirname "$(find /opt/slang -type f -name 'libslang-compiler.so*' | head -n 1)")"; \
    test -n "$SLANG_LIB_DIR"; \
    echo "Using slang lib dir: $SLANG_LIB_DIR"; \
    echo "$SLANG_LIB_DIR" > /etc/ld.so.conf.d/slang.conf; \
    ldconfig; \
    slangc -version


# Runtime shader dir for Docker entrypoint / app
ENV VK_BENCH_SHADER_DIR=/workspace/build/shaders

# NVIDIA runtime hint: ensure Vulkan capability is exposed
ENV NVIDIA_DRIVER_CAPABILITIES=all
RUN which slangc && slangc -version
# ---- Build ----
RUN rm -rf build \
 && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSLANGC=/usr/local/bin/slangc \
 && cmake --build build -v --target vk-bench-shaders vk-bench \
 && ls -lah build/shaders

COPY docker/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]