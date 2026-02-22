FROM nvidia/vulkan:1.3.280-ubuntu22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    vulkan-tools \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ARG SLANG_VERSION=2024.17.2
RUN curl -fsSL -o /tmp/slang.tar.gz https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-linux-x86_64.tar.gz \
    && tar -xzf /tmp/slang.tar.gz -C /tmp \
    && install -m 0755 /tmp/slang-${SLANG_VERSION}/bin/slangc /usr/local/bin/slangc \
    && rm -rf /tmp/slang.tar.gz /tmp/slang-${SLANG_VERSION}

WORKDIR /workspace
COPY CMakeLists.txt /workspace/CMakeLists.txt
COPY src /workspace/src
COPY include /workspace/include
COPY shaders /workspace/shaders
RUN cmake -S /workspace -B /workspace/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /workspace/build --config Release -j"$(nproc)" \
    && install -m 0755 /workspace/build/vk-bench /usr/local/bin/vk-bench \
    && install -d /usr/local/bin/shaders \
    && cp /workspace/build/shaders/*.spv /usr/local/bin/shaders/

COPY docker/entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
