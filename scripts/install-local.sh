#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
app_prefix="${STL_BROWSER_PREFIX:-$HOME/.local}"
[ -x "$source_dir/build/stl-browser" ] || { echo "Build the app first with cmake." >&2; exit 1; }
mkdir -p "$app_prefix/bin" "$app_prefix/share/applications" "$app_prefix/share/icons/hicolor/64x64/apps" "$app_prefix/share/stl-browser"
install -m755 "$source_dir/build/stl-browser" "$app_prefix/bin/stl-browser"
install -m644 "$source_dir/LICENSE" "$app_prefix/share/stl-browser/LICENSE"
install -m644 "$source_dir/xdg/stl-browser.desktop" "$app_prefix/share/applications/stl-browser.desktop"
install -m644 "$source_dir/qt/icons/fstl_64x64.png" "$app_prefix/share/icons/hicolor/64x64/apps/stl-browser.png"
# Use an absolute Exec path so desktop launching also works without ~/.local/bin on PATH.
python3 - "$app_prefix/share/applications/stl-browser.desktop" "$app_prefix/bin/stl-browser" <<'PYCODE'
import pathlib,sys
p=pathlib.Path(sys.argv[1])
# Escape characters reserved inside quoted Desktop Entry Exec arguments.
arg=sys.argv[2].replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$')
exec_path = '"'+arg+'"' if any(c.isspace() or c in '"`$\\' for c in sys.argv[2]) else sys.argv[2]
p.write_text(p.read_text().replace('Exec=stl-browser %f','Exec='+exec_path+' %f'))
PYCODE
if command -v update-desktop-database >/dev/null; then update-desktop-database "$app_prefix/share/applications"; fi
if [ "${1:-}" = "--make-default" ]; then
  app_config="${XDG_CONFIG_HOME:-$HOME/.config}/JoeMirai"
  mkdir -p "$app_config"
  if [ ! -f "$app_config/stl-browser-previous-defaults.txt" ]; then
    for mime in model/stl model/x.stl-ascii model/x.stl-binary application/sla; do
      printf '%s %s\n' "$mime" "$(xdg-mime query default "$mime")"
    done > "$app_config/stl-browser-previous-defaults.txt"
  fi
  for mime in model/stl model/x.stl-ascii model/x.stl-binary application/sla; do
    xdg-mime default stl-browser.desktop "$mime"
  done
fi
printf 'Installed %s/bin/stl-browser\n' "$app_prefix"
