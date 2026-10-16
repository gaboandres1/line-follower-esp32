"""Genera firmware/seguidor_web/web_ui.h (para el ESP32) y docs/ui_preview.html (render con robot simulado).
Uso, desde la raíz del repo:  python3 web/build.py"""
import pathlib, re
H = pathlib.Path(__file__).parent
css = (H / "common.css").read_text()
panel = (H / "panel.html").read_text()
metrics = (H / "metrics.html").read_text()

def page(title, body):
    return ('<!doctype html><html lang="es"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
            f'<title>{title}</title><style>{css}body{{margin:0;background:var(--bg)}}</style></head>'
            f'<body>{body}</body></html>')

def minify(s):  # quita sangrías y líneas vacías (ahorra flash, conserva el JS intacto)
    return "\n".join(l.strip() for l in s.splitlines() if l.strip())

out = ["// Generado por web/build.py — no editar a mano: edita web/ y ejecuta python3 web/build.py.",
       "#pragma once", "#include <Arduino.h>"]
for name, html in (("PAGE_PANEL", page("mirobot", panel)), ("PAGE_METRICS", page("mirobot · métricas", metrics))):
    html = minify(html)
    assert ')rawliteral"' not in html
    out.append(f'const char {name}[] PROGMEM = R"rawliteral({html})rawliteral";')
(H.parent / "firmware" / "seguidor_web" / "web_ui.h").write_text("\n".join(out) + "\n")
print("web_ui.h", (H.parent / "firmware" / "seguidor_web" / "web_ui.h").stat().st_size, "bytes")

# ---- render con simulación ----
tpl = (H / "preview_template.html").read_text()
# el render respeta también el tema elegido en la app (data-theme)
dark = re.search(r"@media \(prefers-color-scheme:dark\)\{:root\{(.*?)\}\}", css, re.S).group(1)
css_pv = css.replace("@media (prefers-color-scheme:dark){:root{", '@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){')
css_pv += ':root[data-theme="dark"]{' + dark + "}"
prev = (tpl.replace("/*CSS*/", css_pv)
           .replace("<!--PANEL-->", panel)
           .replace("<!--METRICS-->", metrics))
(H.parent / "docs" / "ui_preview.html").write_text(prev)
print("ui_preview.html", len(prev), "bytes")
