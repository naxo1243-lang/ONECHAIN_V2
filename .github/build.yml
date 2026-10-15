name: Build macOS arm64 (AU + VST3)

on:
  push:
  workflow_dispatch:

jobs:
  build-macos:
    runs-on: macos-latest   # runner Apple Silicon (M1/M2)
    steps:
      - uses: actions/checkout@v4

      - name: Configurar (arm64)
        run: |
          cmake -S . -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES=arm64 \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

      - name: Compilar AU y VST3
        run: |
          cmake --build build --config Release --target OneChain_AU --parallel
          cmake --build build --config Release --target OneChain_VST3 --parallel

      - name: Firmar (ad-hoc) y empaquetar
        run: |
          mkdir -p dist
          AU=$(find build -name "OneChain.component" -type d | head -1)
          VST=$(find build -name "OneChain.vst3" -type d | head -1)
          echo "AU: $AU"; echo "VST3: $VST"
          for B in "$AU" "$VST"; do
            codesign --force --deep --sign - "$B"
            codesign --verify --verbose "$B"
            ditto "$B" "dist/$(basename "$B")"
          done
          lipo -info "$AU/Contents/MacOS/OneChain"
          ditto -c -k --sequesterRsrc dist OneChain-macOS-arm64.zip

      - name: Validar el AU con auval (lo que exige Logic)
        continue-on-error: true
        run: |
          mkdir -p ~/Library/Audio/Plug-Ins/Components
          cp -R dist/OneChain.component ~/Library/Audio/Plug-Ins/Components/
          killall -9 AudioComponentRegistrar 2>/dev/null || true
          sleep 3
          auval -a | grep -i "Yrnm" || true
          auval -v aufx OnCh Yrnm

      - name: Subir descarga
        uses: actions/upload-artifact@v4
        with:
          name: OneChain-macOS-arm64
          path: OneChain-macOS-arm64.zip
