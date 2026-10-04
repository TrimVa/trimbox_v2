#!/usr/bin/env python3
"""
Intègre la console (index.html) au firmware S3 : compression gzip, puis
tableau C dans trimbox_s3/src/console_gz.h.

Usage : python3 tools/embed_console.py [index.html] [sortie.h]

À relancer après CHAQUE modification de la console, avant de compiler le
firmware. La compilation GitHub le fait d'elle-même ; le fichier .h est
aussi versionné pour permettre une compilation locale directe.
Le résultat est reproductible : même index.html → même .h (horodatage gzip
fixé à 0).
"""
import gzip, re, sys, os, hashlib

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'index.html')
dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '..', 'trimbox_s3', 'src', 'console_gz.h')

html = open(src, 'rb').read()
m = re.search(rb"const CONSOLE_VER\s*=\s*'([^']+)'", html)
ver = m.group(1).decode() if m else '?'
gz = gzip.compress(html, compresslevel=9, mtime=0)
sha = hashlib.sha1(html).hexdigest()[:12]

lines = []
for i in range(0, len(gz), 20):
    lines.append('  ' + ','.join(f'0x{b:02X}' for b in gz[i:i+20]) + ',')
out = f"""// FICHIER GÉNÉRÉ par tools/embed_console.py — NE PAS MODIFIER À LA MAIN.
// Source : index.html, console v{ver}, {len(html)} octets (sha1 {sha}),
// compressée en {len(gz)} octets.
#pragma once
#include <stdint.h>
#include <stddef.h>
#define CONSOLE_EMBED_VER "{ver}"
static const size_t CONSOLE_GZ_LEN = {len(gz)};
static const uint8_t CONSOLE_GZ[] = {{
{chr(10).join(lines)}
}};
"""
open(dst, 'w').write(out)
print(f"console v{ver} : {len(html)} → {len(gz)} octets gzip → {os.path.relpath(dst)}")
