#!/usr/bin/env bash
#
# Mirror the canonical beebium-server formula into the Homebrew tap, pinning it
# to a released tag's source tarball.
#
# Usage:
#   packaging/homebrew/sync-tap.sh <version> [tap-checkout-dirpath]
#
# Example:
#   packaging/homebrew/sync-tap.sh 0.1.0 ~/Code/homebrew-beebium
#
# It downloads the GitHub release source tarball for v<version>, computes its
# sha256, writes the formula with the real url + sha256 into the tap's
# Formula/ directory, and leaves the result staged for the maintainer to commit
# and push. Publishing (the git push to the tap) is deliberately left manual.
set -euo pipefail

here_dirpath="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
formula_filepath="${here_dirpath}/beebium-server.rb"

version="${1:-}"
tap_dirpath="${2:-}"
if [[ -z "${version}" ]]; then
  echo "usage: $0 <version> [tap-checkout-dirpath]" >&2
  exit 2
fi

tag="v${version}"
url="https://github.com/rob-smallshire/beebium/archive/refs/tags/${tag}.tar.gz"

echo "Fetching ${url} to compute sha256..."
tmp_tarball_filepath="$(mktemp -t beebium-${version}.XXXXXX.tar.gz)"
trap 'rm -f "${tmp_tarball_filepath}"' EXIT
curl -fsSL "${url}" -o "${tmp_tarball_filepath}"
sha256="$(shasum -a 256 "${tmp_tarball_filepath}" | awk '{print $1}')"
echo "sha256 = ${sha256}"

# Produce the pinned formula text from the canonical copy.
pinned="$(
  sed \
    -e "s|^  url .*|  url \"${url}\"|" \
    -e "s|^  sha256 .*|  sha256 \"${sha256}\"|" \
    "${formula_filepath}"
)"

if [[ -z "${tap_dirpath}" ]]; then
  echo "----- pinned formula (no tap dir given; printing to stdout) -----"
  echo "${pinned}"
  exit 0
fi

dest_dirpath="${tap_dirpath}/Formula"
mkdir -p "${dest_dirpath}"
dest_filepath="${dest_dirpath}/beebium-server.rb"
echo "${pinned}" > "${dest_filepath}"
echo "Wrote ${dest_filepath}"
echo "Review, then commit and push the tap manually."
