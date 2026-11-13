import base64, json, sys
d = sys.argv[1]; out = sys.argv[2]
html = open(f"{d}/index.html", encoding="utf-8").read()
wasm = base64.b64encode(open(f"{d}/drelva-vave.wasm","rb").read()).decode()
wk = open(f"{d}/worklet.js", encoding="utf-8").read()
a = 'const wasm = await (await fetch("drelva-vave.wasm")).arrayBuffer();'
b = 'await ac.audioWorklet.addModule("worklet.js");'
assert a in html and b in html
html = html.replace(a, 'const wasm = Uint8Array.from(atob(DV_WASM), c => c.charCodeAt(0)).buffer;')
html = html.replace(b, 'await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([DV_WORKLET], {type: "text/javascript"})));')
inj = f'<script>const DV_WASM = "{wasm}";\nconst DV_WORKLET = {json.dumps(wk)};</script>\n'
i = html.index("<script")
html = html[:i] + inj + html[i:]
open(out, "w", encoding="utf-8").write(html)
print(len(html))
