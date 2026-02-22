FROM nvidia/vulkan:1.3.280-ubuntu22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    vulkan-tools \
    glslang-tools \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY CMakeLists.txt /workspace/CMakeLists.txt
COPY src /workspace/src
COPY include /workspace/include
COPY shaders /workspace/shaders
RUN cmake -S /workspace -B /workspace/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /workspace/build --config Release -j"$(nproc)" \
    && install -m 0755 /workspace/build/vk-bench /usr/local/bin/vk-bench

COPY docker/entrypoint.sh /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
