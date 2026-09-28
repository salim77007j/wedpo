#!/usr/bin/env bash
# Wedpo browser — Linux bundle packager.
# Usage: package-linux.sh <path-to-wedpo-browser> <dist-dir> <qt-host-prefix>
# Produces a portable directory tree with the binary, Qt libraries, plugins,
# QtWebEngine process + resources, a qt.conf and a launcher script.
set -euo pipefail

EXE="$1"
DIST="$2"
QT="$3"

QT="${QT:-$(dirname "$(dirname "$(command -v qmake)")")}"
echo "package-linux: exe=$EXE dist=$DIST qt=$QT"

rm -rf "$DIST"
mkdir -p "$DIST/bin" "$DIST/lib" "$DIST/plugins" "$DIST/libexec" "$DIST/share/qt6/resources" "$DIST/share/qt6/translations"

copy_tree_ldd() {  # copy target + all Qt deps it pulls in
    local src="$1"
    local seen=()
    local queue=("$src")
    while [ ${#queue[@]} -gt 0 ]; do
        local cur="${queue[0]}"
        queue=("${queue[@]:1}")
        [ -f "$cur" ] || continue
        local base
        base="$(basename "$cur")"
        if [ ! -f "$DIST/lib/$base" ]; then
            cp -L "$cur" "$DIST/lib/$base"
        fi
        while IFS= read -r dep; do
            [ -f "$dep" ] || continue
            case "$dep" in
                "$QT"/*)
                    local dbase
                    dbase="$(basename "$dep")"
                    if [ ! -f "$DIST/lib/$dbase" ]; then
                        cp -L "$dep" "$DIST/lib/$dbase"
                        queue+=("$dep")
                    fi
                    ;;
            esac
        done < <(ldd "$cur" 2>/dev/null | awk '{if ($3 ~ /^\//) print $3; else if ($1 ~ /^\//) print $1}')
    done
}

# binary
cp "$EXE" "$DIST/bin/wedpo-browser"
copy_tree_ldd "$EXE"

# plugins (with their Qt deps)
for plug in platforms imageformats iconengines tls xcbglintegrations sqldrivers; do
    if [ -d "$QT/plugins/$plug" ]; then
        mkdir -p "$DIST/plugins/$plug"
        for p in "$QT/plugins/$plug"/*.so; do
            [ -e "$p" ] || continue
            cp -L "$p" "$DIST/plugins/$plug/"
            # deps of each plugin
            while IFS= read -r dep; do
                case "$dep" in
                    "$QT"/*) cp -L "$dep" "$DIST/lib/" 2>/dev/null || true ;;
                esac
            done < <(ldd "$p" 2>/dev/null | awk '{if ($3 ~ /^\//) print $3; else if ($1 ~ /^\//) print $1}')
        done
    fi
done

# web engine process + resources + locales
if [ -f "$QT/libexec/QtWebEngineProcess" ]; then
    cp "$QT/libexec/QtWebEngineProcess" "$DIST/libexec/"
    copy_tree_ldd "$QT/libexec/QtWebEngineProcess"
fi
for f in "$QT/resources/"*; do
    [ -e "$f" ] && cp "$f" "$DIST/share/qt6/resources/"
done
if [ -d "$QT/translations/qtwebengine_locales" ]; then
    cp -r "$QT/translations/qtwebengine_locales" "$DIST/share/qt6/translations/"
fi

# rust core
if [ -f "$(dirname "$EXE")/libwedpo_core.so" ]; then
    cp "$(dirname "$EXE")/libwedpo_core.so" "$DIST/lib/"
fi
if [ -f "$(dirname "$EXE")/../rust-core/release/libwedpo_core.so" ]; then
    cp "$(dirname "$EXE")/../rust-core/release/libwedpo_core.so" "$DIST/lib/" 2>/dev/null || true
fi

# qt.conf relocates Qt paths relative to the binary
cat > "$DIST/bin/qt.conf" <<'CONF'
[Paths]
Prefix=..
Libraries=lib
Plugins=plugins
LibraryExecutables=libexec
Data=share/qt6
Translations=share/qt6/translations
Settings=etc
CONF

# launcher
cat > "$DIST/wedpo-browser.sh" <<'RUN'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib:${LD_LIBRARY_PATH:-}"
export QTWEBENGINE_DISABLE_SANDBOX="${QTWEBENGINE_DISABLE_SANDBOX:-0}"
exec "$DIR/bin/wedpo-browser" "$@"
RUN
chmod +x "$DIST/wedpo-browser.sh"

echo "Linux bundle ready at $DIST"
