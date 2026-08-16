/* DigiFrame — local web dashboard + captive portal pages */
#pragma once

/**********************  12b. LOCAL WEB DASHBOARD  ********************/
/* Served by the ESP32 itself: http://digiframe.local on your home WiFi,
 * or http://192.168.4.1 in setup-hotspot mode (captive portal).
 * GIF upload, brightness, messages, character pack, WiFi + Telegram
 * config, live logs. No cloud, no open ports. */
const char DASH_HTML[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>DigiFrame</title>
<style>body{font-family:system-ui;background:#141420;color:#eee;max-width:420px;margin:auto;padding:16px}
h1{color:#ffb3de;font-size:22px}fieldset{border:1px solid #333;border-radius:10px;margin:12px 0;padding:12px}
legend{color:#aab}button,input{border-radius:8px;border:1px solid #444;background:#222;color:#eee;padding:8px;margin:3px 2px}
button{cursor:pointer;background:#ff5078;border:0}li{margin:6px 0;list-style:none}ul{padding:0;margin:6px 0}
.st{font-size:12px;color:#aab;margin-top:4px}
.row{display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin:6px 0}
.row>label{font-size:12px;color:#aab}
.sw{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px;vertical-align:middle;border:1px solid #0006}
#spr{max-height:200px;overflow:auto;border-radius:8px;background:#0e0e18}
#spr:empty{display:none}
.hit{padding:7px 8px;border-bottom:1px solid #23233a;cursor:pointer;font-size:13px}
.hit:last-child{border:0}.hit:hover{background:#23233a}
.hit b{font-weight:600}.hit i{font-style:normal;color:#8892a6;font-size:11px;float:right}
.gh{padding:6px 8px;color:#aab;font-size:12px}
h1{margin:0 0 2px}
/* Tabs with no framework and no extra request: hidden radios drive which pane
   shows. The page was one flat 3,000px column of twelve fieldsets, which put
   the score controls two thirds of the way down a scroll. */
input[name=tab]{display:none}
.tabs{display:flex;gap:4px;margin:10px 0;position:sticky;top:0;background:#141420;padding:6px 0;z-index:5}
.tabs label{flex:1;text-align:center;padding:9px 2px;border-radius:8px;background:#1c1c2b;
 font-size:13px;cursor:pointer;user-select:none}
.pane{display:none}
#t1:checked~#p1,#t2:checked~#p2,#t3:checked~#p3,#t4:checked~#p4{display:block}
#t1:checked~.tabs [for=t1],#t2:checked~.tabs [for=t2],
#t3:checked~.tabs [for=t3],#t4:checked~.tabs [for=t4]{background:#ff5078;color:#fff}
/* status strip */
#strip{font-size:12px;color:#aab;border:1px solid #2a2a3d;border-radius:8px;padding:7px 9px}
#strip b{color:#eee;font-weight:600}
/* live-now rows */
.lv{display:flex;align-items:center;gap:8px;padding:8px;border-bottom:1px solid #23233a;font-size:13px}
.lv:last-child{border:0}
.lv .g{flex:1;min-width:0}
.lv .t{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.lv .m{font-size:11px;color:#8892a6}
.lv button{padding:6px 9px;font-size:12px;white-space:nowrap}
.lv.pin{outline:1px solid #ff5078;border-radius:8px}
#lvw{border-radius:8px;background:#0e0e18;margin:6px 0}
.sp{display:flex;align-items:center;gap:8px;font-size:13px;padding:5px 2px}
.sp .g{flex:1}
/* Destructive, and therefore quiet: unfollow was the loudest control on the
   page when it was a full pink button next to the thing it deletes. */
button.x{background:#2a2a3d;color:#9aa;padding:4px 8px;font-size:12px;line-height:1}
button.x:hover{background:#4a2634;color:#ffb0c4}</style>
</head><body><h1>&#9200; DigiFrame</h1>
<div id=strip>connecting&hellip;</div>
<input type=radio name=tab id=t1 checked><input type=radio name=tab id=t2>
<input type=radio name=tab id=t3><input type=radio name=tab id=t4>
<div class=tabs><label for=t1>Now</label><label for=t2>Scores</label>
<label for=t3>Content</label><label for=t4>Settings</label></div>

<div class=pane id=p1>
<div class=row><button onclick="api('stop')">&#9209; Back to clock</button>
<button onclick=preview()>&#128250; Preview card</button>
<button onclick="api('celebrate')">&#127881; Celebration</button></div>
<fieldset><legend>Live now</legend>
<div id=lvw><div class=gh>looking for matches&hellip;</div></div>
<div class=st>found in your browser, straight from ESPN &mdash; following is not
required to watch one. &#128204; puts a match on the panel until it finishes.</div>
</fieldset>
<fieldset><legend>Send a message</legend>
<input id=m placeholder="Hello!" style="width:64%">
<button onclick="api('msg','t='+encodeURIComponent(m.value))">Send</button></fieldset>
<fieldset><legend>Brightness</legend>
<input type=range min=1 max=255 value=100 id=b aria-label=brightness style="width:100%"
 onchange="api('brightness','v='+b.value)"></fieldset>
</div>

<div class=pane id=p2>
<fieldset><legend>Follow</legend>
<div class=row><label>find <select id=spkind aria-label="what to search" onchange=doSearch()>
<option value=team>teams</option><option value=league>leagues</option></select></label></div>
<input id=spq placeholder="Arsenal, Chennai, Lakers&hellip;" style="width:98%" autocomplete=off
 aria-label="search ESPN">
<div id=spr></div>
<ul id=spl></ul>
<div id=spfb style="display:none"><div class=st>ESPN search is unreachable &mdash; built-in list:</div>
<select id=sps aria-label=sport onchange=loadTeamOpts()></select>
<select id=spt aria-label=team></select>
<button onclick=addTeam()>Follow</button></div></fieldset>
<fieldset><legend>Sports</legend><div id=spsp></div>
<div class=st>a sport you switch off never takes the panel, whatever is live in
it. This is a filter, not a search: it does not go looking for matches on its
own &mdash; follow a league for that.</div></fieldset>
<fieldset><legend>Score card</legend>
<div class=row><label><input type=checkbox id=spe onchange=saveSport()> show live scores</label></div>
<div class=row><label>source <select id=spsrc onchange=saveSport()><option value=http>ESPN (live)</option><option value=demo>demo (simulated)</option></select></label>
<label>effects <select id=spfx onchange=saveSport()><option value=1>major only</option><option value=2>every event</option><option value=0>off</option></select></label></div>
<div class=row><label>each match gets <input id=sprot type=number min=0 max=300 style="width:52px" onchange=saveSport()> s</label>
<span class=st>0 = no rotation</span></div>
<div class=row><label>refresh every <input id=spref type=number min=0 max=300 style="width:52px" onchange=saveSport()> s</label>
<span class=st id=sprefh>0 = per sport</span></div>
<div class=row><label>stay <input id=sph type=number min=0 max=120 style="width:48px" onchange=saveSport()> min after full time</label></div>
<div class=row><select id=spfxt aria-label=animation></select><button onclick=testFx()>Play animation</button></div>
<div class=st>the frame switches to the score card by itself when something you
follow kicks off, and goes back to the clock scene after the match</div></fieldset>
</div>

<div class=pane id=p3>
<fieldset><legend>GIFs (c_* = character pack)</legend><ul id=l></ul>
<input type=file id=f accept=.gif aria-label="GIF file"><br><input id=n placeholder="name" style="width:100px">
<label><input type=checkbox id=p> character pack</label>
<button onclick=up()>Upload</button></fieldset>
<fieldset><legend>Random cameo every</legend>
<input id=iv type=number min=0 value=20 style="width:60px" aria-label="cameo interval"> min (0 = off)
<button onclick="api('interval','m='+iv.value)">Set</button></fieldset>
<fieldset><legend>Special days</legend><ul id=ev></ul>
<input id=ed placeholder="MM-DD" style="width:70px">
<select id=et aria-label="day type"><option value=custom>custom</option><option value=birthday>birthday</option></select>
<input id=em placeholder="message" style="width:98%">
<button onclick=addEv()>Add / update</button>
<div class=st>type drives the visual: custom = fireworks, birthday = cake</div></fieldset>
</div>

<div class=pane id=p4>
<fieldset><legend>WiFi</legend>
<input id=ws placeholder="network name (SSID)" style="width:94%"><br>
<input id=wp type=password placeholder="password" style="width:60%">
<button onclick="api('wifi','s='+encodeURIComponent(ws.value)+'&p='+encodeURIComponent(wp.value))">Save &amp; connect</button>
<div class=st id=wst></div></fieldset>
<fieldset><legend>Telegram</legend>
<input id=tt placeholder="bot token (from @BotFather)" style="width:94%"><br>
<input id=tc placeholder="allowed chat id" style="width:60%">
<button onclick="api('tgconfig','t='+encodeURIComponent(tt.value)+'&c='+encodeURIComponent(tc.value))">Save</button>
<div class=st id=tst></div>
<button onclick="api('tgtest')">Test Telegram send</button></fieldset>
<fieldset><legend>Weather location</legend>
<input id=la placeholder="latitude" style="width:28%">
<input id=lo placeholder="longitude" style="width:28%">
<button onclick="api('loc','la='+encodeURIComponent(la.value)+'&lo='+encodeURIComponent(lo.value))">Save</button>
<div class=st>decimal degrees, e.g. 12.97 / 77.59 &mdash; weather refreshes right away</div></fieldset>
<fieldset><legend>Time zone</legend>
<input id=tz type=number step=0.25 placeholder="UTC offset (hours)" style="width:55%">
<button onclick=saveTz()>Save</button>
<div class=st>e.g. 5.5 for IST, -8 for PST, 5.75 for Nepal &mdash; clock updates right away</div></fieldset>
<fieldset><legend>Home Assistant (MQTT)</legend>
<label><input type=checkbox id=mqe> enable</label><br>
<input id=mqh placeholder="broker host/IP" style="width:60%">
<input id=mqp type=number placeholder="1883" style="width:70px"><br>
<input id=mqu placeholder="username (optional)" style="width:45%">
<input id=mqw type=password placeholder="password" style="width:45%">
<button onclick=saveMqtt()>Save</button>
<div class=st>the clock announces itself to Home Assistant via MQTT discovery</div></fieldset>
<fieldset><legend>Firmware (OTA)</legend>
<input type=file id=fw accept=.bin aria-label="firmware image"><br>
<button onclick=ota()>&#9889; Update firmware</button>
<div class=st id=ost>upload DigiFrame.ino.bin (app image) &mdash; frame reboots when done</div></fieldset>
<fieldset><legend>Logs (live)</legend>
<pre id=log style="background:#0a0a12;padding:8px;border-radius:6px;max-height:220px;overflow:auto;font-size:11px;white-space:pre-wrap;margin:0"></pre>
<button onclick="loadLogs()">&#8635; Refresh</button></fieldset>
</div>
<script>
async function api(ep,body){await fetch('/api/'+ep,{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body||''});load();loadLogs();loadCfg();loadEv()}
async function load(){try{let r=await fetch('/api/list'),j=await r.json();
 l.innerHTML=j.map(g=>`<li>${g} <button onclick="api('play','g=${g}')">&#9654;</button>
 <button onclick="api('del','g=${g}')">&#128465;</button></li>`).join('')}catch(e){}}
async function up(){if(!f.files[0])return;let fd=new FormData();fd.append('file',f.files[0]);
 await fetch('/api/upload?name='+encodeURIComponent(n.value)+'&pack='+(p.checked?'1':'0'),
 {method:'POST',body:fd});load()}
async function loadLogs(){try{let r=await fetch('/api/logs');log.textContent=await r.text();
 log.scrollTop=log.scrollHeight}catch(e){}}
function ota(){if(!fw.files[0]){ost.textContent='pick a .bin first';return}
 if(!confirm('Flash '+fw.files[0].name+' and reboot?'))return;
 let x=new XMLHttpRequest();x.open('POST','/api/ota');
 x.upload.onprogress=e=>{if(e.lengthComputable)ost.textContent='uploading '+Math.round(100*e.loaded/e.total)+'% ...'};
 x.onload=()=>{ost.textContent=x.responseText};
 x.onerror=()=>{ost.textContent='upload failed (connection lost)'};
 let fd=new FormData();fd.append('file',fw.files[0]);x.send(fd);
 ost.textContent='uploading...'}
async function loadCfg(){try{let r=await fetch('/api/config'),j=await r.json();
 ws.placeholder=j.ssid?('SSID: '+j.ssid):'network name (SSID)';
 tc.placeholder=j.chat?('chat id: '+j.chat):'allowed chat id';
 tt.placeholder=j.token?('token: '+j.token):'bot token (from @BotFather)';
 la.placeholder='lat: '+j.lat;lo.placeholder='lon: '+j.lon;
 wst.textContent='WiFi: '+j.wifi;tst.textContent='';
 mqe.checked=!!j.mqttEn;mqh.placeholder=j.mqttHost||'broker host/IP';mqp.placeholder=j.mqttPort||1883;mqu.placeholder=j.mqttUser||'username (optional)';
 spe.checked=!!j.sportEn;spsrc.value=j.sportSrc||'demo';spfx.value=j.sportFx;
 if(!sph.value)sph.value=j.sportHold;if(!sprot.value)sprot.value=j.sportRot;
 if(!spref.value)spref.value=j.sportRef;
 /* the floor is not a guess: every request is spaced 1.2 s apart, so a poll of
    the currently live matches takes as long as it takes. Show what it measured. */
 sprefh.textContent=j.sportPoll?('0 = per sport · last poll took '+(j.sportPoll/1000).toFixed(1)+'s')
                               :'0 = per sport';
 CFG=j;drawStrip(j);
 tz.placeholder='UTC'+(j.tz>=0?'+':'')+(j.tz/3600)+'h'}catch(e){strip.textContent='frame unreachable'}}
/* One line that answers "what is it doing?" without opening a tab. */
function drawStrip(j){const MODE=['clock','message','GIF','celebration','test','setup'];
 let what=j.sportOn?('<b>'+esc(j.sportNow||'score card')+'</b>'+(j.sportPin?' &#128204;':''))
        :'<b>'+(MODE[j.mode]||'clock')+'</b>';
 strip.innerHTML=what+' &middot; '+esc(j.wifi)+' &middot; '+j.heap+'KB'
  +(j.sportErr?' &middot; <span style="color:#ffa0b8">'+esc(j.sportErr)+'</span>':'')}
async function loadEv(){try{let r=await fetch('/api/events'),j=await r.json();
 ev.innerHTML=j.length?j.map(e=>`<li>${e.date} [${e.type}] ${e.message} <button onclick="delEv('${e.date}')">&#128465;</button></li>`).join(''):'<li class=st>none yet</li>'}catch(e){}}
async function addEv(){if(!ed.value)return;await fetch('/api/events',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'d='+encodeURIComponent(ed.value)+'&t='+et.value+'&m='+encodeURIComponent(em.value)});em.value='';loadEv()}
async function delEv(d){await fetch('/api/eventdel',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'d='+encodeURIComponent(d)});loadEv()}
async function saveMqtt(){await api('mqtt','en='+(mqe.checked?'1':'0')+'&h='+encodeURIComponent(mqh.value)+'&p='+(mqp.value||1883)+'&u='+encodeURIComponent(mqu.value)+'&w='+encodeURIComponent(mqw.value))}
async function saveTz(){let h=parseFloat(tz.value);if(isNaN(h))return;await api('tz','s='+Math.round(h*3600))}
/* ---- live scores ----
 Teams come from ESPN's own search, not a list baked into the firmware: the
 frame only knows six sports, but ESPN knows every club in them. The browser
 does the searching because it is the one machine here with memory to spare —
 the ESP32 would have to stream and filter a 135 KB /teams document to do the
 same thing, and could still only offer the leagues it was compiled with.
 That endpoint sends Access-Control-Allow-Origin:* over plain HTTP, which is
 exactly what this page (also plain HTTP) is allowed to call. The catalogue
 baked into the firmware stays as the offline fallback, since the dashboard is
 also served from the setup hotspot where there is no internet at all. */
/* EVERY browser-side call goes to site.WEB.api.espn.com, never site.api —
   they carry the same data but not the same door policy. site.api 403s any
   browser-shaped User-Agent at the Akamai edge (the same allowlist that makes
   the firmware send "ESP32HTTPClient"), and a 403 error page has no CORS
   header, so the browser reports it as a CORS failure and the real cause is
   invisible. A page cannot override its User-Agent — fetch forbids that
   header — so the host IS the fix. The firmware keeps site.api, where its own
   honest UA is welcome. */
const ESPN_HOST='http://site.web.api.espn.com/apis';
const ESPN_SEARCH=ESPN_HOST+'/common/v3/search';
const ESPN_SITE=ESPN_HOST+'/site/v2/sports/';
const ESPN_LEAGUES=ESPN_HOST+'/site/v2/leagues/dropdown';
let CAT=[],SEQ=0,TMR=0,CFG={},FOLLOWS=[];
async function loadCat(){try{let r=await fetch('/api/catalogue');CAT=await r.json();
 /* Favourite sports: a filter over what may reach the panel, not a search of
    its own. Rendered from the registry so a new sport_*.h appears here too. */
 spsp.innerHTML=CAT.map((s,i)=>
  `<div class=sp><label class=g><input type=checkbox data-sp=${i} ${s.on?'checked':''}> ${esc(s.label)}</label>`
  +`<span class=st id=spc${i}></span></div>`).join('');
 spsp.querySelectorAll('[data-sp]').forEach(c=>c.onchange=()=>
  fetch('/api/sportsel',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
   body:'i='+c.dataset.sp+'&v='+(c.checked?'1':'0')}).then(loadTeams));
 sps.innerHTML=CAT.map((s,i)=>`<option value=${i}>${s.label}</option>`).join('');
 /* one flat list of every animation, tagged with the sport that owns it, so
    Preview can put the right card up before Play fires the event on it */
 spfxt.innerHTML=CAT.map((s,i)=>`<optgroup label="${s.label}">`+
  s.fx.map(f=>`<option value="${i}:${f}">${f}</option>`).join('')+'</optgroup>').join('');
 loadTeamOpts()}catch(e){}}
function loadTeamOpts(){let s=CAT[sps.value|0];if(!s)return;
 spt.innerHTML='';for(const t of s.teams){let o=document.createElement('option');o.value=t.id;o.textContent=t.name;spt.appendChild(o)}}
function esc(s){return String(s==null?'':s).replace(/[<>&"]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]))}
function ink(c){c=String(c||'').replace('#','');return /^[0-9a-f]{6}$/i.test(c)?'#'+c:'#666'}
function onSearch(){clearTimeout(TMR);TMR=setTimeout(doSearch,300)}
async function doSearch(){const q=spq.value.trim();
 if(spkind.value=='league')return doLeagueSearch(q);
 if(q.length<3){spr.innerHTML='';return}
 const mine=++SEQ;spr.innerHTML='<div class=gh>searching&hellip;</div>';
 let j;try{const r=await fetch(ESPN_SEARCH+'?limit=25&type=team&query='+encodeURIComponent(q));
  j=await r.json()}catch(e){spfb.style.display='';spr.innerHTML='<div class=gh>ESPN unreachable</div>';return}
 if(mine!=SEQ)return;                       // a later keystroke already won
 spfb.style.display='none';
 /* CAT[].espn is the sport module's own ESPN slug, so a new sport becomes
    searchable here without touching this file */
 let hits=[];
 for(const it of (j.items||[])){const s=CAT.findIndex(c=>c.espn==it.sport);
  if(s>=0)hits.push([s,it])}
 if(!hits.length){spr.innerHTML='<div class=gh>nothing in the six sports this frame shows</div>';return}
 /* ESPN ranks by relevance, which still floats age-group and university sides
    up next to the senior club ("chennai" returns ten of them). Those are the
    entries with no abbreviation, so sink them rather than drop them — someone
    really does follow a second-XI side, they just should not outrank CSK. */
 hits.sort((a,b)=>(a[1].abbreviation?0:1)-(b[1].abbreviation?0:1));
 const more=hits.length-10;hits=hits.slice(0,10);
 spr.innerHTML=hits.map(([s,it],n)=>`<div class=hit data-n=${n}>`+
  `<span class=sw style="background:${ink(it.color)}"></span><b>${esc(it.displayName)}</b>`+
  `<i>${esc(CAT[s].label)}${it.league?' &middot; '+esc(it.league):''}</i></div>`).join('')
  +(more>0?`<div class=gh>+${more} more &mdash; refine the search</div>`:'');
 [...spr.querySelectorAll('.hit')].forEach(el=>el.onclick=()=>follow(hits[el.dataset.n|0]))}
async function follow([s,it]){
 const abbr=(it.abbreviation||it.displayName.slice(0,3)).toUpperCase();
 const r=await fetch('/api/espnfollow',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'s='+encodeURIComponent(CAT[s].key)+'&e='+encodeURIComponent(it.id)+
   '&n='+encodeURIComponent(it.displayName)+'&a='+encodeURIComponent(abbr)+
   '&l='+encodeURIComponent(it.league||'')});
 spr.innerHTML='<div class=gh>'+(r.ok?'following '+esc(it.displayName):await r.text())+'</div>';
 spq.value='';setTimeout(()=>{spr.innerHTML=''},1500);loadTeams()}
/* ---- leagues ----
 ESPN's league list is 570 KB, which is nothing in a browser and impossible on
 the frame — the same reasoning that put the team picker here. It is cached for
 the session because it is one big download per sport. */
const LGC={};
async function leaguesFor(espnSport){
 if(LGC[espnSport])return LGC[espnSport];
 const r=await fetch(ESPN_LEAGUES+'?limit=400&sport='+espnSport);
 const j=await r.json();
 return LGC[espnSport]=(j.leagues||[]).map(l=>({id:l.slug||String(l.id),name:l.name}));}
async function doLeagueSearch(q){
 const mine=++SEQ;spr.innerHTML='<div class=gh>loading leagues&hellip;</div>';
 let hits=[];
 try{for(let s=0;s<CAT.length;s++){
   if(!q&&s>0)break;                       // no query: just show the first sport's
   for(const l of await leaguesFor(CAT[s].espn))
     if(!q||l.name.toLowerCase().includes(q.toLowerCase())||l.id.includes(q.toLowerCase()))
       hits.push([s,l])}}
 catch(e){spfb.style.display='';spr.innerHTML='<div class=gh>ESPN unreachable</div>';return}
 if(mine!=SEQ)return;
 if(!hits.length){spr.innerHTML='<div class=gh>no league matches that</div>';return}
 const more=hits.length-12;hits=hits.slice(0,12);
 spr.innerHTML=hits.map(([s,l],n)=>`<div class=hit data-n=${n}><b>${esc(l.name)}</b>`+
  `<i>${esc(CAT[s].label)} &middot; ${esc(l.id)}</i></div>`).join('')
  +(more>0?`<div class=gh>+${more} more &mdash; refine the search</div>`:'');
 [...spr.querySelectorAll('.hit')].forEach(el=>el.onclick=()=>followLeague(hits[el.dataset.n|0]))}
async function followLeague([s,l]){
 const r=await fetch('/api/leaguefollow',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'s='+encodeURIComponent(CAT[s].key)+'&l='+encodeURIComponent(l.id)+'&n='+encodeURIComponent(l.name)});
 spr.innerHTML='<div class=gh>'+(r.ok?'following '+esc(l.name):await r.text())+'</div>';
 spq.value='';setTimeout(()=>{spr.innerHTML=''},1500);loadTeams();loadLive()}
async function loadTeams(){try{let r=await fetch('/api/teams'),j=await r.json();FOLLOWS=j;spl.innerHTML='';
 CAT.forEach((c,i)=>{const el=document.getElementById('spc'+i);
  if(el)el.textContent=(j.filter(t=>t.si==i).length||'no')+' followed'});
 if(!j.length){spl.innerHTML='<li class=st>nothing followed yet &mdash; search above</li>';loadLive();return}
 for(const t of j){let li=document.createElement('li');
  /* flag every state that silently stops a follow ever reaching the panel */
  let why=!t.on?' &middot; sport off':t.kind=='league'?' &middot; whole league'
        :!t.espn?' &middot; demo only':(!t.lg&&t.sport!='Cricket')?' &middot; no league':'';
  /* a league's "abbr" is just its slug shouted back — show the name alone */
  let head=t.kind=='league'?`<b>${esc(t.name)}</b>`:`<b>${esc(t.abbr)}</b> ${esc(t.name)}`;
  li.innerHTML=head+` <span class=st>${esc(t.sport)}${why}</span> `;
  let b=document.createElement('button');b.className='x';b.innerHTML='&#10006;';
  b.title='unfollow';b.onclick=()=>delTeam(t.key);
  li.appendChild(b);spl.appendChild(li)}
 loadLive()}catch(e){}}
async function addTeam(){let s=CAT[sps.value|0];if(!s)return;
 await fetch('/api/teams',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
 body:'s='+encodeURIComponent(s.key)+'&i='+encodeURIComponent(spt.value)});loadTeams()}
async function delTeam(k){await fetch('/api/teamdel',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'k='+encodeURIComponent(k)});loadTeams()}
async function saveSport(){await api('sports','en='+(spe.checked?'1':'0')+'&s='+spsrc.value+
 '&h='+(sph.value||5)+'&f='+spfx.value+'&r='+(sprot.value===''?30:sprot.value)+
 '&q='+(spref.value===''?0:spref.value))}
/* ---- what is live right now ----
 Fetched HERE, in the browser, not on the frame. The scoreboard carries scores,
 clocks, names and colours in one call, which is exactly what this list wants —
 but it is 147 KB for an in-season NFL Sunday even filtered to one day, so the
 frame must never touch it (it uses the ~350-byte core endpoints instead). The
 browser has no such constraint, and the same CORS grant that makes the pickers
 work applies here. */
/* A two-day UTC span, not "today" locally. ESPN files a fixture under its own
   local date, which is not the viewer's: from IST the NFL games in progress
   right now are filed under yesterday, and a local "today" query returned zero
   events while the span returns all seven. Same fix as espnScanLeague(). */
function span(){const u=d=>d.getUTCFullYear()+String(d.getUTCMonth()+1).padStart(2,'0')+String(d.getUTCDate()).padStart(2,'0');
 const n=new Date();return u(new Date(n-864e5))+'-'+u(n)}
function boards(){                    /* unique sport+league pairs worth asking about */
 const out=[],seen={};
 for(const f of FOLLOWS){const c=CAT[f.si];if(!c||!f.on)continue;
  const lg=f.lg||c.dl;if(!lg)continue;
  const k=c.espn+'/'+lg;if(seen[k])continue;seen[k]=1;out.push({si:f.si,c,lg})}
 return out.slice(0,6)}                // a hard cap: this is a phone on wifi
async function loadLive(){
 const bs=boards();
 if(!bs.length){lvw.innerHTML='<div class=gh>follow a team or a league to see what is on</div>';return}
 let rows=[];
 try{
  const res=await Promise.all(bs.map(b=>
   fetch(ESPN_SITE+b.c.espn+'/'+b.lg+'/scoreboard?dates='+span())
    .then(r=>r.json()).then(j=>[b,j]).catch(()=>null)));
  for(const pair of res){if(!pair)continue;const[b,j]=pair;
   for(const e of (j.events||[])){
    const cp=e.competitions&&e.competitions[0];if(!cp)continue;
    const st=(cp.status&&cp.status.type)||{};
    if(st.state!='in')continue;                       // live only
    const h=(cp.competitors||[]).find(c=>c.homeAway=='home');
    const a=(cp.competitors||[]).find(c=>c.homeAway=='away');
    if(!h||!a)continue;
    rows.push({b,e,cp,st,h,a})}}}
 catch(err){lvw.innerHTML='<div class=gh>could not reach ESPN &mdash; no internet?</div>';return}
 if(!rows.length){lvw.innerHTML='<div class=gh>nothing you follow is playing right now</div>';return}
 lvw.innerHTML=rows.map((r,n)=>{
  const nm=t=>esc(t.team&&(t.team.abbreviation||t.team.displayName)||'?');
  const sc=t=>esc(t.score==null?'':t.score);
  return `<div class="lv${CFG.sportPin==r.e.id?' pin':''}" data-n=${n}>`+
   `<span class=sw style="background:${ink(r.h.team&&r.h.team.color)}"></span>`+
   `<div class=g><div class=t>${nm(r.h)} ${sc(r.h)} &ndash; ${sc(r.a)} ${nm(r.a)}</div>`+
   `<div class=m>${esc(r.st.detail||r.st.description||'')} &middot; ${esc(r.b.c.label)}</div></div>`+
   `<button>${CFG.sportPin==r.e.id?'&#10006;':'&#128204;'}</button></div>`}).join('');
 [...lvw.querySelectorAll('.lv')].forEach(el=>{
  const r=rows[el.dataset.n|0];
  el.querySelector('button').onclick=()=>
   CFG.sportPin==r.e.id?unpin():pin(r)})}
async function pin(r){
 /* a competitor id IS the team id, so these are exactly the ids the frame
    needs to build the core score URLs — no discovery round trip */
 await fetch('/api/pin',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:'s='+encodeURIComponent(CAT[r.b.si].key)+'&l='+encodeURIComponent(r.b.lg)+
   '&e='+encodeURIComponent(r.e.id)+'&h='+encodeURIComponent(r.h.id)+'&a='+encodeURIComponent(r.a.id)});
 await loadCfg();loadLive()}
async function unpin(){await fetch('/api/unpin',{method:'POST'});await loadCfg();loadLive()}
/* Preview the sport the animation dropdown is pointing at, so Play lands on a
   card that owns the event. /api/dev is the reliable way (it freezes the
   poller); without it, fall back to the demo-feed preview. */
async function preview(){const s=spfxt.value.split(':')[0];
 let r=await fetch('/api/dev',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'sport='+s});
 if(!r.ok)await api('scorepreview','v=1')}
async function testFx(){await fetch('/api/scoreevent',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'e='+encodeURIComponent(spfxt.value.split(':')[1]||'')})}
spq.oninput=onSearch;
async function boot(){await loadCat();await loadCfg();await loadTeams();
 load();loadLogs();loadEv()}
boot();
setInterval(loadLogs,2000);
setInterval(loadCfg,5000);       // the status strip is the point of the Now tab
setInterval(loadLive,30000);     // ESPN's own scoreboard cache is ~seconds
</script></body></html>)HTML";

void handleUpload() {
  HTTPUpload &up = web.upload();
  if (up.status == UPLOAD_FILE_START) {
    String nm = web.arg("name");
    if (!nm.length()) nm = up.filename;
    nm.replace(" ", "_");
    if (!nm.endsWith(".gif")) nm += ".gif";
    if (web.arg("pack") == "1" && !nm.startsWith("c_")) nm = "c_" + nm;
    webUpload = LittleFS.open("/" + nm, "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (webUpload) webUpload.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (webUpload) webUpload.close();
  }
}

/* ---- OTA firmware update (dashboard "Firmware" section) ----
 * Streams an uploaded app image (DigiFrame.ino.bin) into the spare OTA
 * slot (the fatflash scheme has app0/app1) via Update, then reboots.
 * The first chunk must carry the esp_app_desc_t magic at offset 0x20 —
 * that's what distinguishes an app image from the bootloader/merged
 * images, so uploading the wrong .bin can't soft-brick the frame.
 * The whole upload is parsed inside one web.handleClient() call, so the
 * render loop never interleaves with flash writes. */
bool     otaBegun     = false;
String   otaError     = "";
uint32_t otaLastShown = 0;

void otaScreen(const String &line) {
  for (int b = 0; b < 2; b++) {          // paint both DMA buffers
    dma->fillScreen(0);
    dma->setTextSize(1);
    dma->setTextColor(C_MSG);
    dma->setCursor(2, 20);
    dma->print("UPDATING");
    dma->setTextColor(C_TEMP);
    dma->setCursor(2, 34);
    dma->print(line);
    panelPresent();
  }
}

void otaFail(const String &why) {
  otaError = why;
  Update.abort();
  otaBegun = false;
  if (tgTaskHandle)      vTaskResume(tgTaskHandle);
  if (weatherTaskHandle) vTaskResume(weatherTaskHandle);
  if (mqttTaskHandle)    vTaskResume(mqttTaskHandle);
  if (sportsTaskHandle)  vTaskResume(sportsTaskHandle);
  mode = MODE_CLOCK;
  logLine("OTA FAILED: " + why);
}

void handleOtaUpload() {
  HTTPUpload &up = web.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaError     = "";
    otaLastShown = 0;
    logLine("OTA start: " + up.filename);
    if (tgTaskHandle)      vTaskSuspend(tgTaskHandle);      // nothing else may
    if (weatherTaskHandle) vTaskSuspend(weatherTaskHandle); // touch heap/flash now
    if (mqttTaskHandle)    vTaskSuspend(mqttTaskHandle);
    if (sportsTaskHandle)  vTaskSuspend(sportsTaskHandle);
    closeGif();
    otaScreen("0 KB");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { otaFail(Update.errorString()); return; }
    otaBegun = true;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaBegun) return;               // already failed — drain the rest silently
    if (up.totalSize == 0) {             // first chunk: verify app-image descriptor
      uint32_t magic = 0;
      if (up.currentSize >= 0x24) memcpy(&magic, up.buf + 0x20, 4);
      if (magic != 0xABCD5432UL) {
        otaFail("not an app image - upload DigiFrame.ino.bin");
        return;
      }
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      otaFail(Update.errorString());
      return;
    }
    uint32_t done = up.totalSize + up.currentSize;
    if (done - otaLastShown > 131072) {  // progress every ~128 KB
      otaLastShown = done;
      otaScreen(String(done / 1024) + " KB");
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaBegun) return;
    otaBegun = false;
    if (Update.end(true)) {              // validates image + sets boot partition
      otaScreen("done!");
      logLine("OTA OK (" + String(up.totalSize / 1024) + " KB) - rebooting");
    } else otaFail(Update.errorString());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (otaBegun) otaFail("upload aborted");
  }
}

void setupWeb() {
  web.on("/", HTTP_GET, []() { web.send_P(200, "text/html", DASH_HTML); });
  web.on("/api/logs", HTTP_GET, []() {
    web.send(200, "text/plain", ctlLogsText());
  });
  web.on("/api/tgtest", HTTP_POST, []() {
    ctlTgTest();
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/list", HTTP_GET, []() {
    web.send(200, "application/json", ctlListGifsJson());
  });
  web.on("/api/msg", HTTP_POST, []() {
    ctlSendMsg(web.arg("t"), false);
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/brightness", HTTP_POST, []() {
    ctlSetBrightness(web.arg("v").toInt());
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/play", HTTP_POST, []() {
    ctlPlayGif(web.arg("g"));
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/del", HTTP_POST, []() {
    ctlDelGif(web.arg("g"));
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/interval", HTTP_POST, []() {
    ctlSetInterval(web.arg("m").toInt());
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/celebrate", HTTP_POST, []() {
    ctlCelebrate();
    web.send(200, "text/plain", "ok");
  });
  /* ---- special days: list / add-update / delete ---- */
  web.on("/api/events", HTTP_GET, []() {
    web.send(200, "application/json", ctlListEventsJson());
  });
  web.on("/api/events", HTTP_POST, []() {
    bool ok = ctlAddEvent(web.arg("d"), web.arg("t"), web.arg("m"));
    web.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "bad date or list full");
  });
  web.on("/api/eventdel", HTTP_POST, []() {
    ctlDelEvent(web.arg("d"));
    web.send(200, "text/plain", "ok");
  });
  /* ---- live scores: favourites, widget config, preview, animation test ---- */
  web.on("/api/catalogue", HTTP_GET, []() {
    web.send(200, "application/json", ctlCatalogueJson());
  });
  web.on("/api/teams", HTTP_GET, []() {
    web.send(200, "application/json", ctlListTeamsJson());
  });
  web.on("/api/teams", HTTP_POST, []() {
    if (!ctlAddTeam(web.arg("s"), web.arg("i"))) {
      web.send(400, "text/plain", "unknown team or list full");
      return;
    }
    web.send(200, "text/plain", "ok");
  });
  /* ---- ESPN team catalogue + following a team by its ESPN id ---- */
  web.on("/api/espnteams", HTTP_GET, []() {
    web.send(200, "application/json", ctlEspnTeamsJson(web.arg("s")));
  });
  web.on("/api/espnrefresh", HTTP_POST, []() {
    ctlRefreshEspnCatalogue(web.arg("s"));
    web.send(200, "text/plain", "queued");
  });
  web.on("/api/espnfollow", HTTP_POST, []() {
    if (!ctlAddEspnTeam(web.arg("s"), web.arg("e"), web.arg("n"),
                        web.arg("a"), web.arg("l"))) {
      web.send(400, "text/plain", "bad sport/id or list full");
      return;
    }
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/teamdel", HTTP_POST, []() {
    ctlDelTeam(web.arg("k"));
    web.send(200, "text/plain", "ok");
  });
  /* ---- follow a whole league; favourite sports; pin a live match ---- */
  web.on("/api/leaguefollow", HTTP_POST, []() {
    if (!ctlFollowLeague(web.arg("s"), web.arg("l"), web.arg("n"))) {
      web.send(400, "text/plain", "bad sport/league or list full");
      return;
    }
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/sportsel", HTTP_POST, []() {
    ctlSetSportEnabled((uint8_t)web.arg("i").toInt(), web.arg("v") == "1");
    web.send(200, "text/plain", "ok");
  });
  /* The dashboard found this match itself, in the browser, off ESPN's
     scoreboard — so it can hand over every id the live tick needs and the
     frame never pays for discovery. */
  web.on("/api/pin", HTTP_POST, []() {
    if (!ctlPinMatch(web.arg("s"), web.arg("l"), web.arg("e"),
                     web.arg("h"), web.arg("a"))) {
      web.send(400, "text/plain", "need sport, event and both team ids");
      return;
    }
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/unpin", HTTP_POST, []() {
    ctlUnpin();
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/sports", HTTP_POST, []() {
    ctlSetSports(web.arg("en") == "1", web.arg("s"),
                 web.arg("h").toInt(), web.arg("f").toInt());
    if (web.hasArg("r")) ctlSetRotate(web.arg("r").toInt());
    if (web.hasArg("q")) ctlSetRefresh(web.arg("q").toInt());
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/scorepreview", HTTP_POST, []() {
    ctlScorePreview(web.arg("v") == "1");
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/scoreevent", HTTP_POST, []() {
    scoreTestEvent(web.arg("e").c_str());
    web.send(200, "text/plain", "ok");
  });
  /* ---- Home Assistant / MQTT config; mqttTask reconnects (core 0) ---- */
  web.on("/api/mqtt", HTTP_POST, []() {
    ctlSetMqtt(web.arg("en") == "1", web.arg("h"), web.arg("p").toInt(),
               web.arg("u"), web.arg("w"));
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/stop", HTTP_POST, []() {
    ctlStop();
    web.send(200, "text/plain", "ok");
  });
  web.on("/api/upload", HTTP_POST,
         []() { web.send(200, "text/plain", "ok"); }, handleUpload);

  /* ---- OTA: upload DigiFrame.ino.bin, flash, reboot ---- */
  web.on("/api/ota", HTTP_POST, []() {
    bool ok = (otaError.length() == 0) && Update.isFinished();
    if (!ok && otaError.length() == 0) otaError = "no file received";
    web.sendHeader("Connection", "close");
    web.send(ok ? 200 : 500, "text/plain",
             ok ? "OK - flashed, rebooting..." : ("FAILED: " + otaError));
    if (ok) { delay(500); ESP.restart(); }
  }, handleOtaUpload);

  /* ---- config: current values (token masked) to prefill the form ---- */
#if DEV_ENDPOINTS
  /* --- dev tool: put the panel into a specific visual state -------------
     Companion to /api/frame — a screenshot is only useful if you can reach
     the screen you want to photograph, and most of these states are not
     otherwise addressable over HTTP (the ambient scene branches on the
     hour and weather code; the score card has one layout per sport).

     Deliberately NOT routed through control.h. That convention exists so
     every front end behaves identically, which is exactly what should not
     happen here: these are debug affordances, not features, and there is
     no Telegram or Home Assistant equivalent to keep in sync. Set
     DEV_ENDPOINTS to 0 in config.h to compile the whole thing out.

     All args optional, applied in this order:
       hour=0-23     spoof the clock hour (scene changes through the day)
       wcode=N       spoof the Open-Meteo weather code (0 clear, 61 rain,
                     73 snow, 45 fog, 95 storm, 2 cloudy)
       sport=N       force the score card to sport index N
       event=NAME    fire a native score event, e.g. WICKET
       celebrate=T   start celebration of type T (custom|birthday)
       msg=TEXT      message for celebrate=
       test=1        run the full /test walk
       clear=1       lift the hour/weather overrides                      */
  web.on("/api/dev", HTTP_POST, []() {
    if (web.hasArg("clear")) {
      if (devWCode >= 0 && devSavedWCode >= 0) wCode = devSavedWCode;
      devHour = devWCode = devSavedWCode = -1;
    }
    if (web.hasArg("hour")) {
      int h = web.arg("hour").toInt();
      devHour = (h >= 0 && h <= 23) ? h : -1;
      if (devHour >= 0) tmNow.tm_hour = devHour;      // take effect this frame
    }
    if (web.hasArg("wcode")) {
      if (devSavedWCode < 0) devSavedWCode = wCode;   // remember the real one once
      devWCode = web.arg("wcode").toInt();
      wCode    = devWCode;
    }
    if (web.hasArg("sport")) {
      // freeze first: sportsDemoForce() writes scoreFront, and an unfrozen
      // sportsTick() would publish a poll over it on the next second
      sportsFreeze = true;
      sportsDemoForce((uint8_t)web.arg("sport").toInt());
    }
    if (web.hasArg("event"))     scoreTestEvent(web.arg("event").c_str());
    if (web.hasArg("celebrate")) startCelebration(web.arg("celebrate"), web.arg("msg"));
    if (web.hasArg("test"))      startTest("");
    web.send(200, "text/plain", "ok");
  });

#if ESPN_ENABLE
  /* --- dev tool: exercise the pure parsers directly ---------------------
     The ESPN provider's trickiest logic is a handful of pure functions —
     the cricket score-string parser, the hex colour, the ISO date, the
     score-delta-to-event mapping. They are only otherwise observable
     through whatever happens to be live on the panel, which is no way to
     catch a regression. This exposes them so tests/ can assert on the
     real firmware rather than on a drifting host-side copy of it. */
  web.on("/api/devtest", HTTP_GET, []() {
    JsonDocument out;
    if (web.hasArg("cricket")) {
      int16_t runs = 0, wkts = 0;
      char overs[8] = "";
      espnCricketScore(web.arg("cricket").c_str(), runs, wkts, overs, sizeof(overs));
      out["runs"] = runs; out["wkts"] = wkts; out["overs"] = overs;
    }
    if (web.hasArg("color"))
      out["rgb565"] = espnColor(web.arg("color").c_str(), RGB565(1, 2, 3));
    if (web.hasArg("ink"))
      out["ink"] = teamInk((uint16_t)web.arg("ink").toInt());
    if (web.hasArg("utc"))
      out["epoch"] = espnParseUtc(web.arg("utc").c_str());
    if (web.hasArg("delta")) {                    // "sportIdx:dScore:dScore2"
      String a = web.arg("delta");
      int c1 = a.indexOf(':'), c2 = a.lastIndexOf(':');
      uint8_t sp = (uint8_t)a.substring(0, c1).toInt();
      const SportModule *mod = sportOf(sp);
      const char *ev = mod->eventForDelta
                     ? mod->eventForDelta(a.substring(c1 + 1, c2).toInt(),
                                          a.substring(c2 + 1).toInt())
                     : nullptr;
      out["event"] = ev ? ev : "";
      out["sport"] = mod->key;
    }
    String body; serializeJson(out, body);
    web.send(200, "application/json", body);
  });
#endif

  /* --- dev tool: panel screenshot (capture.h + dev/panelshot.py) ---------
     Returns the frame currently on the panel as raw RGB565 little-endian:
     PANEL_W*PANEL_H*2 bytes, no header. Capture is armed lazily by the
     first request and auto-disarms 30 s after the last one, so the render
     path costs nothing when nobody is watching.

     The first request after arming has no frame yet and answers 503. It
     cannot simply wait for one: this handler runs inside loop() on core 1,
     which is also the only thing that draws, so blocking here would
     deadlock the very renderer it is waiting on. The client retries. */
  web.on("/api/frame", HTTP_GET, []() {
    if (web.hasArg("off")) {
      dma->disarm();
      web.send(200, "text/plain", "disarmed");
      return;
    }
    if (!dma->arm()) {
      web.send(507, "text/plain", "capture buffers unavailable (PSRAM)");
      return;
    }
    // MODE_SETUP paints its QR once and then never presents again, so an
    // armed capture would wait forever. Force exactly one repaint.
    if (mode == MODE_SETUP) qrLastText = "";
    if (!dma->frameCount) {
      web.send(503, "text/plain", "arming — no frame presented yet");
      return;
    }
    web.sendHeader("X-Frame-Count", String(dma->frameCount));
    web.sendHeader("X-Panel-Size",  String(PANEL_W) + "x" + String(PANEL_H));
    web.sendHeader("X-Mode",        String((int)mode));
    web.send_P(200, "application/octet-stream",
               (PGM_P)dma->shadowFront, CAPTURE_BYTES);
  });
#endif  // DEV_ENDPOINTS

  web.on("/api/config", HTTP_GET, []() {
    web.send(200, "application/json", ctlStatusJson());
  });
  /* ---- save WiFi creds; wifi_manager picks up wifiRetryNow in loop() ---- */
  web.on("/api/wifi", HTTP_POST, []() {
    if (!ctlSetWifi(web.arg("s"), web.arg("p"))) { web.send(400, "text/plain", "SSID required"); return; }
    web.send(200, "text/plain", "ok — connecting to " + cfgWifiSsid);
  });
  /* ---- save weather location; weatherTask refetches right away ---- */
  web.on("/api/loc", HTTP_POST, []() {
    if (!ctlSetLoc(web.arg("la"), web.arg("lo"))) { web.send(400, "text/plain", "bad lat/lon"); return; }
    web.send(200, "text/plain", "ok");
  });
  /* ---- save timezone (UTC offset in seconds); re-applies immediately ---- */
  web.on("/api/tz", HTTP_POST, []() {
    ctlSetTz(web.arg("s").toInt());
    web.send(200, "text/plain", "ok");
  });
  /* ---- save Telegram config; tgTask applies the token (core 0) ---- */
  web.on("/api/tgconfig", HTTP_POST, []() {
    ctlSetTg(web.arg("t"), web.arg("c"));
    web.send(200, "text/plain", "ok");
  });
  /* ---- captive portal: any unknown URL (incl. OS connectivity probes
     like /generate_204, /hotspot-detect.html) redirects to the page ---- */
  web.onNotFound([]() {
    if (portalActive) {
      web.sendHeader("Location", "http://192.168.4.1/", true);
      web.send(302, "text/plain", "");
    } else {
      web.send(404, "text/plain", "not found");
    }
  });
  web.begin();
}
