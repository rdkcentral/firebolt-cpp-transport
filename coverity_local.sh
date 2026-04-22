#!/usr/bin/env bash
# coverity_local.sh — run a Coverity static analysis scan on your local machine
#
# Runs cov-build + cov-analyze inside the same Docker image used by CI.
# Results are written to ./coverity_dir/ and an HTML report to ./coverity_html/.
# No Coverity Connect server connection is made — raw tool output only.
#
# Prerequisites:
#   docker login <DOCKER_REGISTRY> -u <user> -p <apikey>
#   docker pull <DOCKER_REGISTRY>/rdk-docker/docker-rdk-coverity:1.0.7
#
# Usage:
#   sh coverity_local.sh
#   sh coverity_local.sh --image <full-image-ref>   # override image
set -e

IMAGE="${COVERITY_IMAGE:-}"
while [[ $# -gt 0 ]]; do
  case $1 in
    --image) IMAGE="$2"; shift 2;;
    *) echo "Unknown option: $1" >&2; exit 1;;
  esac
done

if [[ -z "$IMAGE" ]]; then
  if [[ -z "${DOCKER_REGISTRY:-}" ]]; then
    echo "ERROR: set DOCKER_REGISTRY or pass --image <full-image-ref>" >&2
    exit 1
  fi
  IMAGE="${DOCKER_REGISTRY}/rdk-docker/docker-rdk-coverity:1.0.7"
fi

docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace \
  "$IMAGE" \
  bash -c '
    set -ex
    export PATH=$PATH:/opt/coverity/bin

    # Install build dependencies
    sh build_dependencies.sh

    # Capture build
    cov-configure --gcc
    cov-build --dir coverity_dir sh cov_build.sh

    # Analyze — same checker set as CI full scan
    cov-analyze --dir coverity_dir \
      --one-tu-per-psf false \
      --disable-spotbugs \
      --aggressiveness-level low \
      --enable DC.STRING_BUFFER \
      --all

    # Emit raw text summary to stdout
    cov-format-errors --dir coverity_dir --emacs-style

    # Emit HTML report for browsing
    mkdir -p coverity_html
    cov-format-errors --dir coverity_dir --html-output coverity_html

    echo ""
    echo "HTML report: coverity_html/index.html"
    echo "Raw database: coverity_dir/"
  '
