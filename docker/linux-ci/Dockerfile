FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc \
    uuid-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
