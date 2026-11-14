#pragma once
#include <Arduino.h>

// Strony WWW SVR. Osobny plik naglowkowy zapobiega mieszaniu HTML/JS z kodem .ino.
const char LOGIN_HTML[] PROGMEM = R"DVLOGIN(
<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#101827"><title>Logowanie | Dziupla Vision</title>
<style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:20px;background:linear-gradient(145deg,#0b1220,#101b2d);color:#edf4ff;font-family:Arial,sans-serif}.box{width:100%;max-width:390px;background:#131e30;border:1px solid #26364d;border-radius:20px;padding:27px;box-shadow:0 20px 60px #0005}.logo{width:48px;height:48px;border-radius:15px;background:linear-gradient(135deg,#187ba8,#55b9ec);display:grid;place-items:center;font-size:24px;font-weight:800;margin-bottom:18px}h1{font-size:22px;margin:0 0 6px}.sub{font-size:13px;color:#91a3ba;margin-bottom:24px}label{display:block;font-size:12px;color:#b8c7d9;margin:14px 0 7px}input{width:100%;padding:13px 14px;border-radius:10px;border:1px solid #344761;background:#0d1727;color:#edf4ff;font-size:15px;outline:none}input:focus{border-color:#55b9ec}button{width:100%;padding:13px;border:0;border-radius:10px;margin-top:21px;background:linear-gradient(90deg,#187ba8,#55b9ec);color:white;font-weight:700;font-size:14px;cursor:pointer}button:disabled{opacity:.6}.error{color:#ff8793;font-size:12px;min-height:18px;margin-top:12px}.foot{color:#71849d;font-size:11px;text-align:center;margin-top:22px}
</style></head><body><form class="box" id="loginForm">
<div class="logo">D</div><h1>Dziupla Vision</h1><div class="sub">Zaloguj się do panelu administracyjnego SVR</div>
<label for="username">Login</label><input id="username" name="login" autocomplete="username" required>
<label for="password">Hasło</label><input id="password" name="password" type="password" autocomplete="current-password" required>
<button id="submit" type="submit">ZALOGUJ SIĘ</button><div class="error" id="error"></div>
<div class="foot">Dziupla Vision · dostęp administracyjny</div></form>
<script>
const form=document.getElementById('loginForm'),btn=document.getElementById('submit'),err=document.getElementById('error');
form.addEventListener('submit',async e=>{e.preventDefault();btn.disabled=true;btn.textContent='LOGOWANIE...';err.textContent='';
try{const body=new URLSearchParams(new FormData(form));const r=await fetch('/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
if(r.ok){location.href='/';}else{err.textContent='Nieprawidłowy login lub hasło.';document.getElementById('password').value='';}
}catch(e){err.textContent='Brak połączenia z serwerem.';}
btn.disabled=false;btn.textContent='ZALOGUJ SIĘ';});
</script></body></html>
)DVLOGIN";

const char INDEX_HTML[] PROGMEM = R"DVINDEX(
<!doctype html><html lang="pl"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#101827"><title>Dziupla Vision | SVR</title>
<style>
:root{color-scheme:dark;--bg:#0b1220;--panel:#131e30;--panel2:#19263a;--line:#26364d;--text:#edf4ff;--muted:#91a3ba;--blue:#55b9ec;--green:#35d49a;--red:#ff6878}*{box-sizing:border-box}body{margin:0;background:linear-gradient(145deg,#0b1220,#101b2d);color:var(--text);font-family:Arial,sans-serif}main{max-width:1120px;margin:auto;padding:22px}header{display:flex;align-items:center;justify-content:space-between;gap:14px;margin-bottom:22px;flex-wrap:wrap}.brand{display:flex;align-items:center;gap:13px}.logo{width:46px;height:46px;border-radius:14px;background:linear-gradient(135deg,#187ba8,#55b9ec);display:grid;place-items:center;font-size:23px;font-weight:800}h1{font-size:22px;margin:0 0 4px}.subtitle{color:var(--muted);font-size:13px}.pill{padding:8px 12px;border:1px solid var(--line);border-radius:30px;color:var(--green);font-size:12px;background:#102a2a}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:13px;margin-bottom:18px}.card{background:linear-gradient(180deg,var(--panel),#111b2b);border:1px solid var(--line);border-radius:16px;padding:17px;min-width:0}.metric-label{font-size:12px;color:var(--muted);margin-bottom:9px}.metric-value{font-size:22px;font-weight:700;overflow-wrap:anywhere}.metric-sub{font-size:11px;color:var(--muted);margin-top:6px}.section-title{font-size:16px;margin:0 0 13px}.nodes{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:13px;margin-bottom:18px}.node-top{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}.node-id{font-weight:800;font-size:18px;letter-spacing:1px}.status{font-size:11px;padding:6px 9px;border-radius:20px;background:#35202a;color:var(--red)}.status.online{background:#12372e;color:var(--green)}.node-name{font-size:13px;color:var(--muted);margin-bottom:12px}.ip{font-family:monospace;font-size:13px;color:var(--blue);margin-bottom:12px}.small{font-size:11px;color:var(--muted);line-height:1.6}.log-list{display:flex;flex-direction:column;gap:8px;max-height:350px;overflow:auto}.log{padding:10px 12px;background:var(--panel2);border:1px solid var(--line);border-radius:10px;font-size:12px;overflow-wrap:anywhere}.log-head{display:flex;justify-content:space-between;gap:10px;color:var(--muted);margin-bottom:5px}.log-device{color:var(--blue);font-weight:700}.footer{color:var(--muted);font-size:11px;text-align:center;margin:20px 0 4px}@media(max-width:760px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.nodes{grid-template-columns:1fr}}@media(max-width:380px){main{padding:13px}.metric-value{font-size:18px}}
</style></head><body><main>
<header><div class="brand"><div class="logo">D</div><div><h1>Dziupla Vision</h1><div class="subtitle">Centralny serwer systemu · SVR</div></div></div><div style="display:flex;gap:9px;align-items:center"><div class="pill" id="systemStatus">● SVR ONLINE</div><button onclick="logout()" style="padding:9px 12px;border:1px solid #344761;border-radius:9px;background:#19263a;color:#edf4ff;cursor:pointer">Wyloguj</button></div></header>
<div class="grid">
<div class="card"><div class="metric-label">Urządzenia online</div><div class="metric-value" id="onlineCount">0 / 3</div><div class="metric-sub">IMG · AI · MAIL</div></div>
<div class="card"><div class="metric-label">Połączeni klienci Wi-Fi</div><div class="metric-value" id="clientCount">0</div><div class="metric-sub">Telefon / komputer / moduły</div></div>
<div class="card"><div class="metric-label">Czas pracy SVR</div><div class="metric-value" id="uptime">--</div><div class="metric-sub">Od uruchomienia serwera</div></div>
<div class="card"><div class="metric-label">Żądania HTTP</div><div class="metric-value" id="requestCount">0</div><div class="metric-sub">Obsłużone przez SVR</div></div></div>
<h2 class="section-title">Monitor urządzeń</h2><div class="nodes" id="nodes"><div class="card">Oczekiwanie na dane...</div></div>
<div class="card"><h2 class="section-title">Dziennik zdarzeń</h2><div class="log-list" id="logs"><div class="small">Brak zdarzeń. Oczekiwanie na moduły.</div></div></div>
<div class="footer">Dziupla Vision · SVR · Automatyczne odświeżanie co 1 sekundę</div></main>
<script>
const $=id=>document.getElementById(id);function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
async function refresh(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);const d=await r.json();const on=d.nodes.filter(n=>n.online).length;$('onlineCount').textContent=on+' / '+d.nodes.length;$('clientCount').textContent=d.clients;$('uptime').textContent=d.uptime;$('requestCount').textContent=d.requests;$('systemStatus').textContent='● SVR ONLINE';$('nodes').innerHTML=d.nodes.map(n=>`<div class="card"><div class="node-top"><div class="node-id">${esc(n.id)}</div><div class="status ${n.online?'online':''}">${n.online?'ONLINE':'OFFLINE'}</div></div><div class="node-name">${esc(n.name)}</div><div class="ip">${esc(n.ip)}</div><div class="small">Ostatni kontakt: ${esc(n.last_seen)}</div><div class="small">Heartbeat: ${n.heartbeats}</div><div class="small">${esc(n.message)}</div></div>`).join('');$('logs').innerHTML=d.logs.length?d.logs.map(l=>`<div class="log"><div class="log-head"><span class="log-device">${esc(l.device)}</span><span>${esc(l.time)}</span></div><div>${esc(l.message)}</div></div>`).join(''):'<div class="small">Brak zdarzeń. Oczekiwanie na moduły.</div>';}catch(e){$('systemStatus').textContent='● BŁĄD POŁĄCZENIA';}}
async function logout(){await fetch('/logout',{method:'POST'});location.href='/';}
refresh();setInterval(refresh,1000);
</script></body></html>
)DVINDEX";

