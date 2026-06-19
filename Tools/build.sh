#!/usr/bin/env bash
#
# Cross-platform build helper for the DesignPatterns plugin.
#
# The plugin is not standalone — UE plugins build as part of a host .uproject. This script either
# (a) builds a host project that contains the plugin, or (b) packages the plugin with RunUAT's
# BuildPlugin (which compiles it for one or more target platforms in isolation).
#
# Usage:
#   ENGINE=/path/to/UE_5.4 ./Tools/build.sh project  <YourProject.uproject> [Win64|Mac|Linux] [Development|Shipping]
#   ENGINE=/path/to/UE_5.4 ./Tools/build.sh plugin   <Win64+Mac+Linux+Android+IOS>
#
# Examples:
#   ENGINE="/c/Program Files/Epic Games/UE_5.4" ./Tools/build.sh plugin Win64+Linux+Android
#   ENGINE="$HOME/UE_5.4" ./Tools/build.sh project ~/Game/Game.uproject Mac Development
#
# Notes:
# - On Windows, run from Git Bash. ENGINE should point at the engine root (the dir containing Engine/).
# - "plugin" mode uses RunUAT BuildPlugin and compiles the plugin for the listed platforms; this is
#   the fastest way to verify the plugin compiles cross-platform without a full game.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPLUGIN="$HERE/Plugins/DesignPatterns/DesignPatterns.uplugin"

if [[ -z "${ENGINE:-}" ]]; then
  echo "ERROR: set ENGINE to your Unreal Engine root, e.g. ENGINE=/path/to/UE_5.4" >&2
  exit 2
fi

# Resolve the platform-correct RunUAT / Build script.
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) RUNUAT="$ENGINE/Engine/Build/BatchFiles/RunUAT.bat"
                        BUILD="$ENGINE/Engine/Build/BatchFiles/Build.bat" ;;
  Darwin)               RUNUAT="$ENGINE/Engine/Build/BatchFiles/RunUAT.sh"
                        BUILD="$ENGINE/Engine/Build/BatchFiles/Mac/Build.sh" ;;
  Linux)                RUNUAT="$ENGINE/Engine/Build/BatchFiles/RunUAT.sh"
                        BUILD="$ENGINE/Engine/Build/BatchFiles/Linux/Build.sh" ;;
  *) echo "Unsupported host OS: $(uname -s)" >&2; exit 2 ;;
esac

MODE="${1:-}"
case "$MODE" in
  plugin)
    PLATFORMS="${2:-Win64}"
    OUT="$HERE/Build/PluginPackage"
    echo ">> BuildPlugin: $UPLUGIN  platforms=$PLATFORMS"
    "$RUNUAT" BuildPlugin \
      -Plugin="$UPLUGIN" \
      -Package="$OUT" \
      -TargetPlatforms="$PLATFORMS" \
      -Rocket
    echo ">> Plugin compiled for [$PLATFORMS] -> $OUT"
    ;;
  project)
    PROJECT="${2:?path to .uproject required}"
    PLATFORM="${3:-Win64}"
    CONFIG="${4:-Development}"
    TARGET="$(basename "$PROJECT" .uproject)Editor"
    echo ">> Build: $TARGET $PLATFORM $CONFIG  (project=$PROJECT)"
    "$BUILD" "$TARGET" "$PLATFORM" "$CONFIG" -Project="$PROJECT" -WaitMutex -FromMsBuild
    echo ">> Built $TARGET for $PLATFORM/$CONFIG"
    ;;
  *)
    echo "Usage: ENGINE=<engine root> $0 {plugin <platforms>|project <uproject> [platform] [config]}" >&2
    exit 2
    ;;
esac
