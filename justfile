set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

build_dir := "build"
flatpak_build_dir := "builddir"
flatpak_repo := "repo"
flatpak_bundle := "fcitx5-vinput.flatpak"
flatpak_app_id := "org.fcitx.Fcitx5.Addon.Vinput"
flatpak_host_app_id := "org.fcitx.Fcitx5"
flatpak_manifest := "packaging/flatpak/ci-manifest.yaml"
flatpak_branch := "stable"

default:
  @just --list

configure type="Release" prefix="/usr" *cmake_args:
  if [ "{{type}}" = "Debug" ]; then \
    cmake --preset debug-clang-mold -DCMAKE_INSTALL_PREFIX={{prefix}} {{cmake_args}}; \
  else \
    cmake --preset release-clang-mold -DCMAKE_INSTALL_PREFIX={{prefix}} {{cmake_args}}; \
  fi

dev prefix="/usr" *cmake_args:
  cmake --preset debug-clang-mold -DCMAKE_INSTALL_PREFIX={{prefix}} {{cmake_args}}

configure-release prefix="/usr" *cmake_args:
  cmake --preset release-clang-mold -DCMAKE_INSTALL_PREFIX={{prefix}} {{cmake_args}}

release ref="main" version="":
  #!/usr/bin/env bash
  set -euo pipefail
  version_value="{{version}}"
  if [ -z "${version_value}" ]; then
    version_value="$(tr -d '\n' < VERSION)"
  fi
  git tag "v${version_value}" {{ref}}
  git push origin "v${version_value}"

channels ref="main" version="" ppa_revision="":
  #!/usr/bin/env bash
  set -euo pipefail
  version_value="{{version}}"
  ppa_revision_value="{{ppa_revision}}"
  if [ -z "${version_value}" ]; then
    version_value="$(tr -d '\n' < VERSION)"
  fi
  extra_fields=()
  if [ -n "${ppa_revision_value}" ]; then
    extra_fields+=(--field ppa_revision="${ppa_revision_value}")
  fi
  gh workflow run channels.yml \
    --ref main \
    --field ref="{{ref}}" \
    --field version="${version_value}" \
    "${extra_fields[@]}"

build:
  cmake --build --preset release-clang-mold

install prefix="/usr":
  @cmake --preset release-clang-mold -DCMAKE_INSTALL_PREFIX={{prefix}}
  @cmake --install {{build_dir}}
  @echo "note: run 'systemctl --user daemon-reload' after installing systemd user units"
  @if [ "{{prefix}}" = "/usr" ]; then just check-stale-install; fi

doctor: check-build-cache check-installed-binaries check-user-service check-stale-install

check-build-cache:
  @echo "== Build Cache =="
  @cmake -LA -N {{build_dir}} 2>/dev/null | rg 'CMAKE_BUILD_TYPE|CMAKE_INSTALL_PREFIX' || true
  @echo

check-installed-binaries:
  @echo "== Installed Binaries =="
  @for cmd in vinput vinput-daemon vinput-gui; do \
    if command -v "$cmd" >/dev/null 2>&1; then \
      path="$(command -v "$cmd")"; \
      echo "$cmd -> $path"; \
      ls -l "$path"; \
    else \
      echo "$cmd -> (not found)"; \
    fi; \
    echo; \
  done

check-user-service:
  @echo "== User Service =="
  @systemctl --user cat vinput-daemon.service || true
  @echo

check-stale-install:
  @echo "== Stale /usr/local Paths =="
  @found=0; test ! -e /usr/local/bin/vinput || { found=1; ls -ld /usr/local/bin/vinput; }; test ! -e /usr/local/bin/vinput-daemon || { found=1; ls -ld /usr/local/bin/vinput-daemon; }; test ! -e /usr/local/bin/vinput-gui || { found=1; ls -ld /usr/local/bin/vinput-gui; }; test ! -e /usr/local/share/systemd/user/vinput-daemon.service || { found=1; ls -ld /usr/local/share/systemd/user/vinput-daemon.service; }; test ! -e /usr/local/share/dbus-1/services/org.fcitx.Vinput.service || { found=1; ls -ld /usr/local/share/dbus-1/services/org.fcitx.Vinput.service; }; test ! -e /usr/local/share/applications/vinput-gui.desktop || { found=1; ls -ld /usr/local/share/applications/vinput-gui.desktop; }; test "$found" -eq 0 && echo "(none)" || true
  @echo

clean:
  rm -rf {{build_dir}}

rebuild type="Release" prefix="/usr" *cmake_args: clean
  if [ "{{type}}" = "Debug" ]; then \
    cmake --preset debug-clang-mold -DCMAKE_INSTALL_PREFIX={{prefix}} {{cmake_args}}; \
    cmake --build --preset debug-clang-mold; \
  else \
    cmake --preset release-clang-mold -DCMAKE_INSTALL_PREFIX={{prefix}} {{cmake_args}}; \
    cmake --build --preset release-clang-mold; \
  fi

sherpa version="" prefix="/usr" archive="":
  bash scripts/build-sherpa-onnx.sh "{{version}}" "{{prefix}}" "{{archive}}"

check-i18n:
  python3 scripts/check-i18n.py

source-archive:
  bash scripts/create-source-archive.sh

flatpak-build manifest=flatpak_manifest:
  flatpak-builder --user --force-clean --repo={{flatpak_repo}} --ccache {{flatpak_build_dir}} {{manifest}}

flatpak-bundle repo=flatpak_repo bundle=flatpak_bundle app_id=flatpak_app_id branch=flatpak_branch:
  flatpak build-bundle --runtime {{repo}} {{bundle}} {{app_id}} {{branch}}

flatpak-install bundle=flatpak_bundle:
  flatpak install --user -y {{bundle}}

flatpak-permissions app_id=flatpak_host_app_id:
  flatpak override --user --filesystem=xdg-run/pipewire-0 {{app_id}}
  flatpak override --user --filesystem=xdg-config/systemd:create {{app_id}}

# Build Debian package using Docker
# Example with proxy (uncomment and adjust as needed):
# deb:
#     @DOCKER_BUILDKIT=1 docker build \
#         --add-host=host.docker.internal:host-gateway \
#         --build-arg all_proxy=socks5h://host.docker.internal:1080 \
#         --build-arg http_proxy=http://host.docker.internal:1081 \
#         --build-arg https_proxy=http://host.docker.internal:1081 \
#         --target exporter \
#         -t fcitx5-vinput-builder \
#         -f Dockerfile.debian \
#         --output type=local,dest=. .
deb:
    @DOCKER_BUILDKIT=1 docker build \
        --add-host=host.docker.internal:host-gateway \
        --build-arg all_proxy=${all_proxy:-""} \
        --build-arg http_proxy=${http_proxy:-""} \
        --build-arg https_proxy=${https_proxy:-""} \
        --target exporter \
        -t fcitx5-vinput-builder \
        -f Dockerfile.debian \
        --output type=local,dest=. .
