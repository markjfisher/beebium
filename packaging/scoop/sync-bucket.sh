#!/usr/bin/env bash
#
# Mirror the canonical beebium-server Scoop manifest into the bucket, pinning it
# to a published release's Windows .zip asset.
#
# Usage:
#   packaging/scoop/sync-bucket.sh <version> [bucket-checkout-dirpath]
#
# Example:
#   packaging/scoop/sync-bucket.sh 0.1.0 ~/Code/scoop-beebium
#
# It downloads the released .zip for v<version>, computes its sha256, writes the
# manifest with the real url + hash into the bucket's bucket/ directory, and
# leaves it staged for the maintainer to commit and push.
#
# NOTE: unlike the Homebrew tap (which builds from the source archive, available
# for any tag), the Scoop manifest points at the .zip RELEASE ASSET, so the
# GitHub Release for v<version> must be PUBLISHED first -- otherwise the download
# URL 404s.
set -euo pipefail

here_dirpath="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
manifest_filepath="${here_dirpath}/beebium-server.json"

version="${1:-}"
bucket_dirpath="${2:-}"
if [[ -z "${version}" ]]; then
  echo "usage: $0 <version> [bucket-checkout-dirpath]" >&2
  exit 2
fi

url="https://github.com/rob-smallshire/beebium/releases/download/v${version}/beebium-server-${version}-windows-x64.zip"

echo "Fetching ${url} to compute sha256..."
tmp_zip_filepath="$(mktemp -t beebium-${version}.XXXXXX.zip)"
trap 'rm -f "${tmp_zip_filepath}"' EXIT
curl -fsSL "${url}" -o "${tmp_zip_filepath}"
sha256="$(shasum -a 256 "${tmp_zip_filepath}" | awk '{print $1}')"
echo "sha256 = ${sha256}"

# Pin version + the concrete 64bit url/hash. The autoupdate block keeps its
# literal $version template (Scoop expands it on future releases), so leave it.
pinned="$(
  jq --arg v "${version}" --arg url "${url}" --arg hash "${sha256}" '
    .version = $v
    | .architecture["64bit"].url = $url
    | .architecture["64bit"].hash = $hash
  ' "${manifest_filepath}"
)"

if [[ -z "${bucket_dirpath}" ]]; then
  echo "----- pinned manifest (no bucket dir given; printing to stdout) -----"
  echo "${pinned}"
  exit 0
fi

dest_dirpath="${bucket_dirpath}/bucket"
mkdir -p "${dest_dirpath}"
dest_filepath="${dest_dirpath}/beebium-server.json"
echo "${pinned}" > "${dest_filepath}"
echo "Wrote ${dest_filepath}"
echo "Review, then commit and push the bucket manually."
