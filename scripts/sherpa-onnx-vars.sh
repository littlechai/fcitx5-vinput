#!/usr/bin/env bash

SHERPA_ONNX_REPO="k2-fsa/sherpa-onnx"

sherpa_onnx_set_vars() {
    local version="${1:?version is required}"
    local target_arch="${2:-$(uname -m)}"
    local arch
    local suffix="shared-no-tts"

    case "${target_arch}" in
        x86_64)
            arch="x64"
            ;;
        aarch64)
            arch="aarch64"
            suffix="shared-cpu"
            ;;
        *)
            arch="${target_arch}"
            ;;
    esac

    SHERPA_ONNX_VERSION="${version}"
    SHERPA_ONNX_ARCHIVE="sherpa-onnx-v${version}-linux-${arch}-${suffix}.tar.bz2"
    SHERPA_ONNX_STRIP_DIR="sherpa-onnx-v${version}-linux-${arch}-${suffix}"
    SHERPA_ONNX_URL="https://github.com/${SHERPA_ONNX_REPO}/releases/download/v${version}/${SHERPA_ONNX_ARCHIVE}"
    SHERPA_ONNX_SHA256=""

    case "${version}" in
        1.13.4)
            if [[ "${target_arch}" == "aarch64" ]]; then
                SHERPA_ONNX_SHA256="36c5a3c942358ed635471488f50a28a96181331c935b0dce75a02b7f49913dc2"
            elif [[ "${target_arch}" == "x86_64" ]]; then
                SHERPA_ONNX_SHA256="e61f550ab6fe44a0c00f862df7e0985d95ca0fc1883688337ae4816378bd2727"
            fi
            ;;
    esac
}

# Fetch the sha256 digest for an asset from the GitHub API.
# This is only used as a fallback when the version is not in the built-in map.
# Usage: sherpa_onnx_fetch_digest <version> <asset_name>
sherpa_onnx_fetch_digest() {
    local version="${1:?version is required}"
    local asset_name="${2:?asset_name is required}"
    local api_url="https://api.github.com/repos/${SHERPA_ONNX_REPO}/releases/tags/v${version}"

    curl -fsSL "${api_url}" \
        | jq -re --arg name "${asset_name}" \
            '.assets[] | select(.name == $name) | .digest // empty
             | ltrimstr("sha256:")'
}

sherpa_onnx_set_vars "${SHERPA_ONNX_VERSION:-1.13.4}"
