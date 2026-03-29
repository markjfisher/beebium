# Linux Development Container

A Docker container mirrors the GitHub Actions Linux CI environment, allowing
Linux builds and tests to be run locally from a macOS (or Windows) host.

## Prerequisites

- Docker Desktop (or another Docker-compatible runtime)

## Building the Image

From the repository root:

```bash
docker build -t beebium-linux-ci docker/linux-ci/
```

This creates an Ubuntu 24.04 image with the same packages the CI workflow
installs (`cmake`, `g++`, `libgrpc++-dev`, `libprotobuf-dev`,
`protobuf-compiler-grpc`, `uuid-dev`).

Rebuild the image after editing `docker/linux-ci/Dockerfile` or when you want
to pick up newer Ubuntu package versions.

## Starting a Container

Mount the repository read-only and keep the container running in the
background:

```bash
docker run -d --name beebium-linux \
  -v "$(pwd)":/workspace/beebium:ro \
  -w /workspace \
  beebium-linux-ci \
  sleep infinity
```

The source is mounted read-only so that builds inside the container cannot
modify the host working tree. The container uses `/workspace` as its working
directory; the build tree lives entirely inside the container filesystem.

## Configuring and Building

Copy the source into a writable area inside the container, then configure and
build as normal:

```bash
# One-time copy (or repeat after source changes on the host)
docker exec beebium-linux bash -c \
  "rm -rf /workspace/src && cp -r /workspace/beebium /workspace/src && rm -rf /workspace/src/build /workspace/src/cmake-build-debug"

# Configure
docker exec beebium-linux bash -c \
  "cd /workspace/src && cmake -B build-linux \
     -DCMAKE_BUILD_TYPE=Release \
     -DBEEBIUM_BUILD_TESTS=ON \
     -DBEEBIUM_BUILD_SERVICE=ON \
     -DBEEBIUM_BUILD_SERVER=ON"

# Build (--parallel 2 to stay within typical CI memory limits)
docker exec beebium-linux bash -c \
  "cd /workspace/src && cmake --build build-linux --parallel 2"
```

Use `--parallel 2` to match CI. Higher parallelism may trigger OOM on
memory-constrained Docker configurations.

### Incremental Rebuilds

After editing files on the host, re-copy only the changed files rather than the
entire tree:

```bash
docker exec beebium-linux bash -c \
  "cp /workspace/beebium/src/extensions/test-scratch-ram/TestScratchRam.cpp \
      /workspace/src/src/extensions/test-scratch-ram/TestScratchRam.cpp"

docker exec beebium-linux bash -c \
  "cd /workspace/src && cmake --build build-linux --target test_scratch_ram --parallel 2"
```

Or re-copy everything for a clean slate:

```bash
docker exec beebium-linux bash -c \
  "rm -rf /workspace/src && cp -r /workspace/beebium /workspace/src && rm -rf /workspace/src/build /workspace/src/cmake-build-debug"
```

## Running Tests

### Full Test Suite

```bash
docker exec beebium-linux bash -c \
  "ctest --test-dir /workspace/src/build-linux --output-on-failure"
```

### Specific Tests

Run a single test by name:

```bash
docker exec beebium-linux bash -c \
  "ctest --test-dir /workspace/src/build-linux \
     -I 239,243 --output-on-failure"
```

Or run a test executable directly for more control over Catch2 options:

```bash
docker exec beebium-linux bash -c \
  "./workspace/src/build-linux/tests/test_scratch_ram --reporter compact"
```

### Listing Tests

```bash
docker exec beebium-linux bash -c \
  "ctest --test-dir /workspace/src/build-linux -N" | grep -i scratch
```

## Interactive Shell

For exploratory debugging, open a shell inside the container:

```bash
docker exec -it beebium-linux bash
```

## Stopping and Removing the Container

```bash
docker stop beebium-linux && docker rm beebium-linux
```

The container holds only build artefacts (the source copy and the build tree).
Nothing is lost by removing it.

## Differences from CI

The container closely mirrors the `server-linux` CI job but there are minor
differences to be aware of:

| Aspect | CI | Local container |
|--------|-----|-----------------|
| Runner architecture | x86_64 | Matches host (arm64 on Apple Silicon, x86_64 on Intel) |
| Ubuntu version | `ubuntu-latest` (currently 24.04) | `ubuntu:24.04` (pinned in Dockerfile) |
| Build parallelism | `--parallel 2` | User's choice (use 2 to match CI) |
| Source checkout | Fresh clone | Bind-mounted read-only copy |
| Build directory | `build/` | `build-linux/` (to avoid confusion with host build) |

The architecture difference matters: if the host is Apple Silicon, the
container runs under Docker's aarch64 emulation of Ubuntu, not x86_64.
GCC codegen bugs that are architecture-specific may not reproduce. Most
GCC optimisation issues (such as `-O2` devirtualisation bugs with multiple
inheritance) are architecture-independent and will reproduce on either.

## Keeping the Image Up to Date

When the CI workflow changes its package list (in `.github/workflows/ci.yml`,
the `Install dependencies` step of the `server-linux` job), update
`docker/linux-ci/Dockerfile` to match and rebuild the image.
