#pragma once
#include <Arduino.h>

// Strona WWW modulu IMG.
const char PAGE[] PROGMEM = R"IMGPAGE(
<!doctype html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#101827">
<title>Dziupla Vision | IMG</title>
<style>
:root{color-scheme:dark;--bg:#0b1220;--panel:#131e30;--line:#26364d;--text:#edf4ff;--muted:#91a3ba;--blue:#55b9ec;--green:#35d49a;--red:#ff6878}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(145deg,#0b1220,#101b2d);color:var(--text);font-family:Arial,sans-serif;min-height:100vh}
main{max-width:760px;margin:auto;padding:22px}.top{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:20px;flex-wrap:wrap}
.brand{display:flex;align-items:center;gap:12px}.logo{width:45px;height:45px;border-radius:14px;background:linear-gradient(135deg,#187ba8,#55b9ec);display:grid;place-items:center;font-size:22px;font-weight:800}
h1{font-size:21px;margin:0 0 4px}.sub{font-size:12px;color:var(--muted)}.pill{padding:8px 12px;border:1px solid #245344;border-radius:30px;color:var(--green);font-size:12px;background:#102a2a}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:17px;padding:20px;margin-bottom:15px}
h2{font-size:16px;margin:0 0 16px}.drop{border:1px dashed #46617f;border-radius:13px;padding:26px 14px;text-align:center;background:#0d1727}
.drop strong{display:block;margin:9px 0 5px}.muted{color:var(--muted);font-size:12px}
input[type=file]{display:block;width:100%;margin-top:15px;color:var(--muted);font-size:12px}
label{display:block;font-size:12px;color:#b8c7d9;margin:17px 0 7px}
input[type=email]{width:100%;padding:13px;border:1px solid #344761;border-radius:10px;background:#0d1727;color:var(--text);font-size:15px}
button{width:100%;padding:14px;border:0;border-radius:10px;margin-top:17px;background:linear-gradient(90deg,#187ba8,#55b9ec);color:white;font-weight:700;font-size:14px;cursor:pointer}
button:disabled{opacity:.55;cursor:wait}.preview{display:none;width:100%;max-height:320px;object-fit:contain;margin-top:15px;border-radius:10px;background:#080e18}
.progress{height:9px;background:#0b1422;border-radius:20px;overflow:hidden;margin-top:14px;display:none}.bar{height:100%;width:0;background:linear-gradient(90deg,#187ba8,#55b9ec);transition:width .15s}
.status{padding:12px;border-radius:10px;background:#0d1727;color:var(--muted);font-size:13px;line-height:1.5;overflow-wrap:anywhere}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:9px}.stat{background:#0d1727;border:1px solid var(--line);border-radius:10px;padding:12px}.stat b{display:block;font-size:14px;margin-top:6px}.stat span{font-size:11px;color:var(--muted)}
@media(max-width:520px){main{padding:13px}.panel{padding:15px}.stats{grid-template-columns:1fr}}
</style></head>
<body><main>
<header class="top"><div class="brand"><div class="logo">D</div><div><h1>Dziupla Vision · IMG</h1><div class="sub">Moduł przygotowania i przesyłania zdjęć</div></div></div><div class="pill" id="connection">Łączenie z SVR...</div></header>
<section class="panel">
<h2>Nowe zgłoszenie listu</h2>
<div class="drop"><div style="font-size:30px">▧</div><strong>Wybierz zdjęcie listu</strong><div class="muted">JPG, PNG lub zdjęcie z telefonu</div><input id="file" type="file" accept="image/*" capture="environment"></div>
<img id="preview" class="preview" alt="Podgląd zdjęcia">
<label for="email">Adres e-mail odbiorcy</label>
<input id="email" type="email" maxlength="100" placeholder="odbiorca@example.com" required>
<button id="send">PRZYGOTUJ I WYŚLIJ DO SVR</button>
<div class="progress" id="progress"><div class="bar" id="bar"></div></div>
<div class="status" id="status" style="margin-top:13px">Wybierz zdjęcie i wpisz adres e-mail.</div>
</section>
<section class="panel">
<h2>Format danych dla AI</h2>
<div class="stats">
<div class="stat"><span>Rozdzielczość</span><b>240 × 240 px</b></div>
<div class="stat"><span>Format</span><b>RGB888</b></div>
<div class="stat"><span>Rozmiar</span><b>172 800 B</b></div>
</div>
</section>
<section class="panel">
<h2>Stan modułu</h2>
<div class="stats">
<div class="stat"><span>Adres IMG</span><b>192.168.4.10</b></div>
<div class="stat"><span>Serwer SVR</span><b>192.168.4.1</b></div>
<div class="stat"><span>Wysłane obrazy</span><b id="sentCount">0</b></div>
</div>
</section>
</main>
<script>
const $=id=>document.getElementById(id);
let selected=null;
$('file').addEventListener('change',()=>{
 selected=$('file').files[0]||null;
 if(!selected){$('preview').style.display='none';return;}
 const url=URL.createObjectURL(selected);$('preview').src=url;$('preview').style.display='block';
 $('status').textContent='Wybrano: '+selected.name+' ('+Math.round(selected.size/1024)+' KB).';
});
function setStatus(text,ok=false){$('status').textContent=text;$('status').style.color=ok?'#35d49a':'#edf4ff';}
function makeRgbBlob(file){
 return new Promise((resolve,reject)=>{
  const image=new Image();
  image.onload=()=>{
   try{
    const canvas=document.createElement('canvas');canvas.width=240;canvas.height=240;
    const ctx=canvas.getContext('2d',{willReadFrequently:true});
    // Kadrowanie centralne z zachowaniem proporcji (cover).
    const scale=Math.max(240/image.width,240/image.height);
    const w=image.width*scale,h=image.height*scale;
    ctx.fillStyle='#ffffff';ctx.fillRect(0,0,240,240);
    ctx.drawImage(image,(240-w)/2,(240-h)/2,w,h);
    const rgba=ctx.getImageData(0,0,240,240).data;
    const rgb=new Uint8Array(240*240*3);
    for(let s=0,d=0;s<rgba.length;s+=4){rgb[d++]=rgba[s];rgb[d++]=rgba[s+1];rgb[d++]=rgba[s+2];}
    resolve(new Blob([rgb],{type:'application/octet-stream'}));
   }catch(e){reject(e);}
  };
  image.onerror=()=>reject(new Error('Nie można odczytać zdjęcia.'));
  image.src=URL.createObjectURL(file);
 });
}
$('send').addEventListener('click',async()=>{
 const email=$('email').value.trim();
 if(!selected){setStatus('Najpierw wybierz zdjęcie.');return;}
 if(!$('email').checkValidity()||!email){setStatus('Wpisz prawidłowy adres e-mail.');return;}
 $('send').disabled=true;$('progress').style.display='block';$('bar').style.width='0%';
 try{
  setStatus('Przygotowywanie obrazu 240 × 240 RGB...');
  const blob=await makeRgbBlob(selected);
  if(blob.size!==172800)throw new Error('Nieprawidłowy rozmiar obrazu: '+blob.size+' B');
  const form=new FormData();
  form.append('email',email); // pole tekstowe przed plikiem
  form.append('image',blob,'image.rgb');
  setStatus('Wysyłanie obrazu do SVR...');
  const xhr=new XMLHttpRequest();
  const result=await new Promise((resolve,reject)=>{
   xhr.open('POST','http://192.168.4.1/api/image',true);
   xhr.upload.onprogress=e=>{if(e.lengthComputable)$('bar').style.width=Math.round(e.loaded/e.total*100)+'%';};
   xhr.onload=()=>{let data;try{data=JSON.parse(xhr.responseText);}catch(e){data={};}
    if(xhr.status>=200&&xhr.status<300)resolve(data);else reject(new Error(data.error||('SVR HTTP '+xhr.status)));};
   xhr.onerror=()=>reject(new Error('Błąd połączenia z SVR.'));
   xhr.ontimeout=()=>reject(new Error('Przekroczono czas oczekiwania.'));
   xhr.timeout=120000;xhr.send(form);
  });
  $('bar').style.width='100%';
  if(result&&result.ok===false)throw new Error(result.error||'SVR odrzucił obraz.');
  $('sentCount').textContent=String(Number($('sentCount').textContent)+1);
  setStatus('SVR odebrał obraz i przekazał go do AI. Odpowiedź: '+JSON.stringify(result),true);
 }catch(e){setStatus('BŁĄD: '+e.message);}
 finally{$('send').disabled=false;}
});
</script></body></html>
)IMGPAGE";
