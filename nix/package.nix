{
  lib,
  stdenv,
  cmake,
  pkg-config,
  gettext,
  extra-cmake-modules,
  fcitx5,
  sherpa-onnx,
  onnxruntime,
  nlohmann_json,
  cli11,
  curl,
  libarchive,
  openssl,
  systemd,
  pipewire,
  qt6,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "fcitx5-vinput";
  version = "1.1.1";

  src = lib.cleanSource ./..;

  nativeBuildInputs = [
    cmake
    pkg-config
    gettext
    extra-cmake-modules
    qt6.wrapQtAppsHook
    qt6.qttools
  ];

  buildInputs = [
    fcitx5
    sherpa-onnx
    onnxruntime
    nlohmann_json
    cli11
    curl
    libarchive
    openssl
    systemd
    pipewire
    qt6.qtbase
  ];

  cmakeFlags = [
    "-DVINPUT_RUNTIME_MODE=system"
    (lib.cmakeBool "VINPUT_FETCH_CLI11" false)
  ];

  meta = {
    description = "Offline voice input addon for Fcitx5 with optional OpenAI-compatible post-processing";
    homepage = "https://github.com/fcitx5-vinput/fcitx5-vinput";
    license = lib.licenses.gpl3Plus;
    maintainers = [ ];
    platforms = lib.platforms.linux;
    mainProgram = "vinput";
  };
})
