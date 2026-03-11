FROM nvidia/cuda:13.1.1-cudnn-devel-ubuntu24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libvulkan1 \
    libvulkan-dev \
    vulkan-tools \
    mesa-vulkan-drivers \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ARG SLANG_VERSION=2026.3.1
RUN set -eux; \
    mkdir -p /opt/slang; \
    curl -fsSL -o /tmp/slang.tar.gz \
      "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-linux-x86_64.tar.gz"; \
    tar -xzf /tmp/slang.tar.gz -C /opt/slang; \
    rm -f /tmp/slang.tar.gz; \
    SLANGC_PATH="$(find /opt/slang -type f -name slangc -perm -111 | head -n 1)"; \
    test -n "$SLANGC_PATH"; \
    install -m 0755 "$SLANGC_PATH" /usr/local/bin/slangc; \
    SLANG_LIB_DIR="$(dirname "$(find /opt/slang -type f -name 'libslang-compiler.so*' | head -n 1)")"; \
    test -n "$SLANG_LIB_DIR"; \
    echo "$SLANG_LIB_DIR" > /etc/ld.so.conf.d/slang.conf; \
    ldconfig; \
    slangc -version

WORKDIR /workspace
COPY CMakeLists.txt /workspace/CMakeLists.txt
COPY cmake /workspace/cmake
COPY src /workspace/src
COPY include /workspace/include
COPY shaders /workspace/shaders
RUN cmake -S /workspace -B /workspace/build -DCMAKE_BUILD_TYPE=Release \
    -DVK_BENCH_ENABLE_WINDOW=OFF \
    -DVK_BENCH_FETCH_GLFW=OFF \
    -DSLANGC=/usr/local/bin/slangc \
    && cmake --build /workspace/build --config Release -j"$(nproc)" \
    && install -m 0755 /workspace/build/vk-bench /usr/local/bin/vk-bench \
    && install -d /usr/local/bin/shaders \
    && cp /workspace/build/shaders/*.spv /usr/local/bin/shaders/

COPY docker/entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
