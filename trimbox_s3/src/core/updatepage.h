// ============================================================================
//  Page de SECOURS de mise à jour du firmware (GET /update).
//  La voie normale est la section « Mise à jour du firmware » de la console ;
//  cette page minimale reste utilisable même si la console embarquée était
//  défaillante. Aucune dépendance, quelques centaines d'octets.
// ============================================================================
#pragma once
static const char UPDATE_PAGE[] = R"HTML(<!DOCTYPE html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TrimBox — mise à jour</title>
<style>
:root{color-scheme:light dark;--v:#7C4DFF}
body{font-family:system-ui,sans-serif;max-width:480px;margin:0 auto;padding:24px 16px;background:Canvas;color:CanvasText}
h1{font-size:20px}button{background:var(--v);color:#fff;border:0;border-radius:6px;padding:12px 18px;font-weight:700;font-size:15px;width:100%;margin-top:12px}
button:disabled{opacity:.5}input{width:100%;margin:8px 0}progress{width:100%;height:18px;margin-top:12px}
#m{margin-top:12px;white-space:pre-wrap}.ok{color:#0A7A5A}.err{color:#C81E45}
</style></head><body>
<h1>Mise à jour du firmware</h1>
<p>Fichier <b>trimbox_s3-app.bin</b> (artefact <i>firmware-s3-app</i> de la compilation GitHub).
La voiture doit être arrêtée et l'enregistrement arrêté.</p>
<input type="file" id="f" accept=".bin">
<button id="b">Envoyer</button>
<progress id="p" max="100" value="0" hidden></progress>
<div id="m"></div>
<p><a href="/">← console</a></p>
<script>
const $=i=>document.getElementById(i);
$('b').onclick=()=>{
  const f=$('f').files[0]; if(!f){$('m').textContent='Choisissez un fichier.';return;}
  const x=new XMLHttpRequest(); $('b').disabled=true; $('p').hidden=false; $('m').textContent='Envoi…'; $('m').className='';
  x.upload.onprogress=e=>{ if(e.lengthComputable) $('p').value=e.loaded/e.total*100; };
  x.onload=()=>{ $('m').className=x.status==200?'ok':'err'; $('m').textContent=x.responseText||('erreur '+x.status);
    if(x.status==200) setTimeout(()=>location.href='/',9000); else $('b').disabled=false; };
  x.onerror=()=>{ $('m').className='err'; $('m').textContent='Connexion perdue pendant l\'envoi.'; $('b').disabled=false; };
  x.open('POST','/update'); x.setRequestHeader('Content-Type','application/octet-stream'); x.send(f);
};
</script></body></html>)HTML";
