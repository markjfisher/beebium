# Homebrew formula for the headless Beebium emulator servers.
#
# This is the canonical copy, kept in the monorepo for development, review and
# CI. It is mirrored into the tap (rob-smallshire/homebrew-beebium) at release
# time by packaging/homebrew/sync-tap.sh.
#
# The servers are built from source against Homebrew's own gRPC/protobuf/abseil.
# Since the ExtensionRpc channel landed, extension plugins no longer embed gRPC
# (they link only libbeebium_extension_api + the shared libprotobuf), so there
# is no duplicate-runtime hazard and no need to static-link gRPC the way the
# Linux bundle does.
class BeebiumServer < Formula
  desc "Headless BBC Micro emulator servers for Model B, B+, B+ 128K and ROM/RAM"
  homepage "https://github.com/rob-smallshire/beebium"
  url "https://github.com/rob-smallshire/beebium/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000" # placeholder; pinned by sync-tap.sh
  license "GPL-3.0-or-later"
  head "https://github.com/rob-smallshire/beebium.git", branch: "master"

  depends_on "cmake" => :build
  depends_on "nlohmann-json" => :build
  depends_on "abseil"
  depends_on "c-ares"
  depends_on "grpc"
  depends_on "openssl@3"
  depends_on "protobuf"
  depends_on "re2"

  def install
    # Build only the server executables (and their extension plugins); the test
    # suite, Python/TS clients and the macOS app are out of scope for the
    # server package.
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args,
           "-DBEEBIUM_BUILD_TESTS=OFF"
    system "cmake", "--build", "build", "--target", "beebium-servers"

    # Install the whole relocatable tree under libexec: the four server
    # binaries (libexec/bin), the extension ABI dylibs (libexec/lib), the
    # dlopened plugins (libexec/bin/extensions/<name>) and the ROMs/presets
    # (libexec/share/beebium). The binaries resolve all of these relative to
    # their own on-disk location, so the tree relocates intact.
    system "cmake", "--install", "build", "--prefix", libexec

    # Put the four servers on the user's PATH without dragging the rest of the
    # tree (notably bin/extensions) onto it. Discovery follows the symlink to
    # the real binary in libexec, so the relative resource lookups still work.
    %w[
      beebium-model-b
      beebium-model-b-plus
      beebium-model-b-plus-128k
      beebium-model-b-romram
    ].each do |server|
      bin.install_symlink libexec/"bin"/server
    end
  end

  test do
    # list-extensions exercises the full discovery path: it loads the extension
    # ABI dylib via RPATH and enumerates both the built-in and the dlopened
    # plugins. Run it through the PATH symlink so the test also covers the
    # symlink-resolution that real invocations use.
    output = shell_output("#{bin}/beebium-model-b list-extensions")

    # The built-in extensions are compiled into the binary.
    assert_match "host-serial", output
    assert_match "aun", output
    # A representative dlopened plugin must be discovered from the keg.
    assert_match "scsi-hdd", output
    # The plugins must resolve out of the installed tree, not a build dir.
    assert_match (libexec/"bin/extensions").to_s, output
  end
end
