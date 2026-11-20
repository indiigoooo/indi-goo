// INDI GOO - Web pendataan limbah bengkel (server C++ + antarmuka web tertanam)
// Kompilasi (Linux/macOS/MinGW): g++ -std=c++17 indigoo_server.cpp -o indigoo -pthread
// Windows MinGW: tambahkan -lws2_32 -lmswsock
// Jalankan: ./indigoo  lalu buka http://localhost:8080
// Butuh httplib.h (cpp-httplib) di folder yang sama. Data tersimpan di indigoo_data.tsv
#include "httplib.h"
#include <fstream>
#include <sstream>
#include <map>
#include <mutex>
#include <ctime>
using namespace std;

typedef vector<string> Row;
map<string, vector<Row>> T;  // B=bengkel, S=setoran, P=penjemputan, H=harga acuan
mutex M;
const char* DB = "indigoo_data.tsv";

string clean(string s) {
  for (auto& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return s.substr(0, 200);
}
string jesc(const string& s) {
  string o;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c < 32) o += ' ';
    else o += c;
  }
  return o;
}
string now() {
  char b[32]; time_t t = time(nullptr);
  strftime(b, sizeof b, "%Y-%m-%d %H:%M", localtime(&t));
  return b;
}
void save() {
  ofstream f(DB);
  for (auto& kv : T) for (auto& r : kv.second) {
    f << kv.first;
    for (auto& x : r) f << '\t' << x;
    f << '\n';
  }
}
void load() {
  ifstream f(DB); string l;
  while (getline(f, l)) {
    stringstream ss(l); string p, t; Row r;
    getline(ss, t, '\t');
    while (getline(ss, p, '\t')) r.push_back(p);
    if (!t.empty()) T[t].push_back(r);
  }
}
int nid(const string& t) { return T[t].empty() ? 1 : stoi(T[t].back()[0]) + 1; }
bool hasB(const string& id) { for (auto& r : T["B"]) if (r[0] == id) return true; return false; }
bool num(const string& s, double& v) {
  try { size_t p; v = stod(s, &p); return p == s.size() && v > 0 && v <= 100000; }
  catch (...) { return false; }
}
string P(const httplib::Request& q, const char* k) { return q.has_param(k) ? clean(q.get_param_value(k)) : ""; }
void out(httplib::Response& r, bool ok, const string& e = "") {
  r.set_content(ok ? string("{\"ok\":true}") : "{\"ok\":false,\"err\":\"" + jesc(e) + "\"}", "application/json");
}
string state() {
  string s = "{"; bool f = true;
  for (string t : {"B", "S", "P", "H"}) {
    if (!f) s += ","; f = false;
    s += "\"" + t + "\":["; bool a = true;
    for (auto& r : T[t]) {
      if (!a) s += ","; a = false;
      s += "[";
      for (size_t i = 0; i < r.size(); i++) { if (i) s += ","; s += "\"" + jesc(r[i]) + "\""; }
      s += "]";
    }
    s += "]";
  }
  return s + "}";
}

const string PAGE = string(R"HTML(<!doctype html><html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>INDI GOO - Pendataan Limbah Bengkel</title>
<style>
:root{--g:#1e6b3a;--d:#12351f;--l:#e8f3ea;--b:#cfdcd2}
*{box-sizing:border-box}body{margin:0;font:15px/1.5 system-ui,Segoe UI,sans-serif;color:#1b2a20;background:#f7faf7}
header{background:var(--d);color:#fff;padding:14px 20px}header b{font-size:22px;letter-spacing:.5px}header small{display:block;color:#a9d3b3}
nav{display:flex;flex-wrap:wrap;gap:6px;padding:10px 20px;background:var(--l);border-bottom:1px solid var(--b)}
nav button{border:1px solid var(--g);background:#fff;color:var(--g);padding:7px 12px;border-radius:6px;cursor:pointer}
nav button.on{background:var(--g);color:#fff}button:focus-visible,input:focus-visible,select:focus-visible{outline:3px solid #f2b705}
main{padding:16px 20px;max-width:1100px;margin:auto}h3{margin:20px 0 8px;color:var(--d)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}
.card{background:#fff;border:1px solid var(--b);border-left:5px solid var(--g);padding:10px 12px}.card small{display:block;color:#51655a}.card b{font-size:21px}
.f{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0;align-items:center}.f label{min-width:290px}
input,select{padding:8px;border:1px solid #9db3a4;border-radius:6px;min-width:170px;font:inherit}
main button{background:var(--g);color:#fff;border:0;padding:8px 14px;border-radius:6px;cursor:pointer;font:inherit}
.sc{overflow-x:auto}table{border-collapse:collapse;width:100%;background:#fff}th,td{border:1px solid var(--b);padding:6px 9px;text-align:left;vertical-align:top}th{background:var(--l)}
.warn{background:#fff6dc;border:1px solid #e3c25c;padding:9px 12px;border-radius:6px}.qr{display:inline-block;text-align:center;margin:2px 6px}
@media print{header,nav,.f button{display:none}}
</style></head><body>
<header><b>INDI GOO</b><small>Recycle, Connect, Grow - pendataan limbah bengkel</small></header>
<nav><button data-t="dash" onclick="go('dash')">Dasbor</button><button data-t="bengkel" onclick="go('bengkel')">Bengkel mitra</button>
<button data-t="setor" onclick="go('setor')">Catat setoran</button><button data-t="jemput" onclick="go('jemput')">Penjemputan</button>
<button data-t="laporan" onclick="go('laporan')">Laporan bulanan</button><button data-t="harga" onclick="go('harga')">Harga acuan</button></nav>
<main id="main">Memuat...</main><script>
)HTML") + string(R"HTML(
let D={B:[],S:[],P:[],H:[]},h={},tab='dash';
const J=['Oli','Besi','Aluminium','Komponen'],U={Oli:'liter',Besi:'kg',Aluminium:'kg',Komponen:'unit'},CAP={OLI:200,LGM:100,KMP:20},
FIN=['Ditimbang dan dibayar','Diserahkan ke mitra berizin'],
HL={besi_beli:'Besi - harga beli dari bengkel (Rp/kg)',besi_jual:'Besi - harga jual ke pembeli (Rp/kg)',alu_beli:'Aluminium - harga beli (Rp/kg)',alu_jual:'Aluminium - harga jual (Rp/kg)',kmp_beli:'Komponen - harga beli (Rp/unit)',kmp_jual:'Komponen - harga jual (Rp/unit)',oli_mitra:'Oli - harga beli mitra berizin (Rp/liter)',oli_komisi:'Komisi koordinasi INDI GOO untuk oli (Rp/liter)',langganan:'Layanan kemitraan digital (Rp/bulan)'};
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const rp=n=>'Rp'+Math.round(n).toLocaleString('id-ID'),v=id=>document.getElementById(id).value;
const kode=(b,t)=>'IG-'+t+'-'+String(b).padStart(3,'0'),bn=id=>(D.B.find(b=>b[0]==id)||[0,'?'])[1];
const fin=s=>FIN.includes(s[6]);
const fill=(c,t)=>D.S.filter(s=>s[2]==c&&!fin(s)).reduce((a,s)=>a+ +s[4],0)/CAP[t]*100;
function money(s){const j=+s[4],k=s[3];if(k=='Oli')return{bayar:j*h.oli_mitra,laba:j*h.oli_komisi};
 const m={Besi:'besi',Aluminium:'alu',Komponen:'kmp'}[k];return{bayar:j*h[m+'_beli'],laba:j*(h[m+'_jual']-h[m+'_beli'])}}
async function load(){D=await(await fetch('/api/state')).json();h={};D.H.forEach(r=>h[r[0]]=+r[1]);render()}
async function post(u,o){const j=await(await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(o)})).json();
 if(!j.ok)alert(j.err);else await load();return j.ok}
function go(t){tab=t;render()}
function render(){document.querySelectorAll('nav button').forEach(b=>b.classList.toggle('on',b.dataset.t==tab));
 document.getElementById('main').innerHTML=({dash:vDash,bengkel:vBengkel,setor:vSetor,jemput:vJemput,laporan:vLaporan,harga:vHarga})[tab]()}
function tblS(r,act){return `<div class=sc><table><tr><th>Waktu<th>Bengkel<th>Kode<th>Jenis<th>Jumlah<th>Status${act?'<th>':''}</tr>${r.map(s=>`<tr><td>${s[5]}<td>${esc(bn(s[1]))}<td>${s[2]}<td>${s[3]}<td>${s[4]} ${U[s[3]]}<td>${s[6]}${act?`<td>${fin(s)?'':`<button onclick="post('/api/status',{id:${s[0]},status:'${s[3]=='Oli'?FIN[1]:FIN[0]}'})">Tandai selesai</button>`}`:''}</tr>`).join('')||'<tr><td colspan=7>Belum ada data.</tr>'}</table></div>`}
function vDash(){const f=D.S.filter(fin),sm=k=>f.filter(s=>s[3]==k).reduce((a,s)=>a+ +s[4],0);
 let by=0,lb=D.B.length*(h.langganan||0);f.forEach(s=>{const m=money(s);by+=m.bayar;lb+=m.laba});
 let al='';D.B.forEach(b=>Object.keys(CAP).forEach(t=>{const p=fill(kode(b[0],t),t);if(p>=80)al+=`<li>${esc(b[1])}: wadah ${kode(b[0],t)} terisi ${p.toFixed(0)}%. Jadwalkan penjemputan.</li>`}));
 return `<div class=grid>${[['Bengkel mitra',D.B.length],['Oli bekas diserahkan ke mitra berizin',sm('Oli')+' liter'],['Logam teralihkan',(sm('Besi')+sm('Aluminium'))+' kg'],['Komponen dipakai ulang',sm('Komponen')+' unit'],['Dibayarkan ke bengkel',rp(by)],['Estimasi pendapatan INDI GOO (margin, komisi, langganan)',rp(lb)]].map(x=>`<div class=card><small>${x[0]}</small><b>${x[1]}</b></div>`).join('')}</div>
 <h3>Wadah terisi 80% atau lebih</h3><ul>${al||'<li>Tidak ada.</li>'}</ul><h3>Setoran terbaru</h3>${tblS(D.S.slice(-8).reverse(),false)}`}
function vBengkel(){return `<h3>Daftarkan bengkel mitra</h3><div class=f><input id=n placeholder="Nama bengkel"><input id=a placeholder=Alamat><input id=k placeholder="Kontak / WhatsApp"><button onclick="post('/api/bengkel',{nama:v('n'),alamat:v('a'),kontak:v('k')})">Simpan bengkel</button></div>
 <p><small>Setiap bengkel otomatis mendapat tiga wadah berkode QR: tong oli (OLI), bin logam (LGM), dan bin komponen (KMP). Gambar QR memerlukan internet.</small></p>
 <div class=sc><table><tr><th>ID<th>Bengkel<th>Alamat<th>Kontak<th>Kode QR wadah</tr>${D.B.map(b=>`<tr><td>${b[0]}<td>${esc(b[1])}<td>${esc(b[2])}<td>${esc(b[3])}<td>${Object.keys(CAP).map(t=>`<div class=qr><img width=70 height=70 alt="${kode(b[0],t)}" src="https://api.qrserver.com/v1/create-qr-code/?size=140x140&data=${kode(b[0],t)}"><br><small>${kode(b[0],t)}</small></div>`).join('')}</tr>`).join('')||'<tr><td colspan=5>Belum ada bengkel.</tr>'}</table></div>`}
function vSetor(){const codes=D.B.flatMap(b=>Object.keys(CAP).map(t=>kode(b[0],t)));
 return `<h3>Catat setoran (hasil pindai QR)</h3><div class=warn>Tidak diterima: aki bekas, filter oli bekas, kain majun, dan kemasan oli (limbah B3 yang memerlukan izin). Oli bekas hanya dicatat dan dikoordinasikan; penjemputan dan pembelian dilakukan mitra berizin.</div>
 <div class=f><input id=kd list=cl placeholder="Kode wadah, mis. IG-OLI-001"><datalist id=cl>${codes.map(c=>`<option>${c}`).join('')}</datalist><select id=jn>${J.map(j=>`<option>${j}`).join('')}</select><input id=jm type=number min=0 step=any placeholder="Jumlah (liter / kg / unit)"><button onclick="post('/api/setor',{kode:v('kd').toUpperCase(),jenis:v('jn'),jumlah:v('jm')})">Catat setoran</button></div>
 <h3>Riwayat setoran</h3>${tblS(D.S.slice().reverse(),true)}`}
function vJemput(){const o=D.B.map(b=>`<option value=${b[0]}>${esc(b[1])}`).join('');
 return `<h3>Permintaan penjemputan</h3><div class=f><select id=pb>${o}</select><select id=pj>${J.map(j=>`<option>${j}`).join('')}</select><input id=pc placeholder="Catatan / jadwal"><button onclick="post('/api/jemput',{bid:v('pb'),jenis:v('pj'),catatan:v('pc')})">Minta penjemputan</button></div>
 <div class=sc><table><tr><th>Waktu<th>Bengkel<th>Jenis<th>Penjemput<th>Catatan<th>Status<th></tr>${D.P.slice().reverse().map(p=>`<tr><td>${p[4]}<td>${esc(bn(p[1]))}<td>${p[2]}<td>${p[2]=='Oli'?'Mitra berizin':'INDI GOO'}<td>${esc(p[3])}<td>${p[5]}<td>${p[5]=='Selesai'?'':`<button onclick="post('/api/jemput_selesai',{id:${p[0]}})">Tandai selesai</button>`}</tr>`).join('')||'<tr><td colspan=7>Belum ada permintaan.</tr>'}</table></div>`}
function vLaporan(){const el=id=>document.getElementById(id),m=el('lm')?v('lm'):new Date().toISOString().slice(0,7),b=el('lb')?v('lb'):(D.B[0]||[''])[0];
 const f=D.S.filter(s=>s[1]==b&&s[5].startsWith(m)&&fin(s));let tot=0;
 const rows=J.map(j=>{const x=f.filter(s=>s[3]==j),q=x.reduce((a,s)=>a+ +s[4],0),p=x.reduce((a,s)=>a+money(s).bayar,0);tot+=p;return `<tr><td>${j}<td>${q} ${U[j]}<td>${rp(p)}</tr>`}).join('');
 return `<h3>Laporan bulanan bengkel</h3><div class=f><select id=lb onchange=render()>${D.B.map(x=>`<option value=${x[0]}${x[0]==b?' selected':''}>${esc(x[1])}`).join('')}</select><input id=lm type=month value=${m} onchange=render()><button onclick=print()>Cetak laporan</button></div>
 <div class=sc><table><tr><th>Jenis<th>Jumlah tercatat<th>Pembayaran ke bengkel</tr>${rows}<tr><td colspan=2>Biaya layanan kemitraan<td>-${rp(h.langganan)}</tr><tr><th colspan=2>Pendapatan bersih bengkel<th>${rp(tot-h.langganan)}</tr></table></div>`}
function vHarga(){return `<h3>Harga acuan</h3><p><small>Nilai awal adalah asumsi proposal. Ubah sesuai kesepakatan dan hasil survei; perubahan berlaku pada seluruh perhitungan.</small></p>${Object.keys(HL).map(k=>`<div class=f><label>${HL[k]}</label><input id=h_${k} type=number value=${h[k]}><button onclick="post('/api/harga',{key:'${k}',val:v('h_${k}')})">Simpan harga</button></div>`).join('')}`}
load();
</script></body></html>)HTML");

int main() {
  load();
  if (T["H"].empty()) {
    const char* d[][2] = {{"besi_beli","3500"},{"besi_jual","4500"},{"alu_beli","9000"},{"alu_jual","12000"},
      {"kmp_beli","25000"},{"kmp_jual","35000"},{"oli_mitra","2000"},{"oli_komisi","300"},{"langganan","50000"}};
    for (auto& x : d) T["H"].push_back({x[0], x[1]});
    save();
  }
  httplib::Server svr;
  svr.Get("/", [](const httplib::Request&, httplib::Response& r) { r.set_content(PAGE, "text/html; charset=utf-8"); });
  svr.Get("/api/state", [](const httplib::Request&, httplib::Response& r) {
    lock_guard<mutex> l(M); r.set_content(state(), "application/json");
  });
  svr.Post("/api/bengkel", [](const httplib::Request& q, httplib::Response& r) {
    lock_guard<mutex> l(M);
    string n = P(q, "nama");
    if (n.empty()) { out(r, false, "Nama bengkel wajib diisi"); return; }
    T["B"].push_back({to_string(nid("B")), n, P(q, "alamat"), P(q, "kontak"), now()});
    save(); out(r, true);
  });
  svr.Post("/api/setor", [](const httplib::Request& q, httplib::Response& r) {
    lock_guard<mutex> l(M);
    string kode = P(q, "kode"), jenis = P(q, "jenis"), jml = P(q, "jumlah"), bid; double v;
    if (kode.size() < 8 || kode.substr(0, 3) != "IG-") { out(r, false, "Kode wadah tidak valid"); return; }
    string t = kode.substr(3, 3);
    try { bid = to_string(stoi(kode.substr(7))); } catch (...) { out(r, false, "Kode wadah tidak valid"); return; }
    if (!hasB(bid)) { out(r, false, "Bengkel untuk kode ini belum terdaftar"); return; }
    bool ok = (t == "OLI" && jenis == "Oli") || (t == "LGM" && (jenis == "Besi" || jenis == "Aluminium")) || (t == "KMP" && jenis == "Komponen");
    if (!ok) { out(r, false, "Jenis limbah tidak sesuai wadah. Aki, filter oli, majun, dan kemasan oli tidak diterima."); return; }
    if (!num(jml, v)) { out(r, false, "Jumlah harus angka lebih dari 0"); return; }
    T["S"].push_back({to_string(nid("S")), bid, kode, jenis, jml, now(), t == "OLI" ? "Menunggu mitra berizin" : "Menunggu ditimbang"});
    save(); out(r, true);
  });
  svr.Post("/api/status", [](const httplib::Request& q, httplib::Response& r) {
    lock_guard<mutex> l(M);
    string id = P(q, "id"), st = P(q, "status");
    for (auto& s : T["S"]) if (s[0] == id) {
      bool ok = s[3] == "Oli" ? st == "Diserahkan ke mitra berizin" : st == "Ditimbang dan dibayar";
      if (!ok) { out(r, false, "Status tidak sesuai jenis limbah"); return; }
      s[6] = st; save(); out(r, true); return;
    }
    out(r, false, "Data tidak ditemukan");
  });
  svr.Post("/api/jemput", [](const httplib::Request& q, httplib::Response& r) {
    lock_guard<mutex> l(M);
    string b = P(q, "bid"), j = P(q, "jenis");
    if (!hasB(b)) { out(r, false, "Pilih bengkel terlebih dahulu"); return; }
    if (j != "Oli" && j != "Besi" && j != "Aluminium" && j != "Komponen") { out(r, false, "Jenis tidak valid"); return; }
    T["P"].push_back({to_string(nid("P")), b, j, P(q, "catatan"), now(), "Diminta"});
    save(); out(r, true);
  });
  svr.Post("/api/jemput_selesai", [](const httplib::Request& q, httplib::Response& r) {
    lock_guard<mutex> l(M);
    for (auto& p : T["P"]) if (p[0] == P(q, "id")) { p[5] = "Selesai"; save(); out(r, true); return; }
    out(r, false, "Data tidak ditemukan");
  });
  svr.Post("/api/harga", [](const httplib::Request& q, httplib::Response& r) {
    lock_guard<mutex> l(M);
    double v;
    if (!num(P(q, "val"), v)) { out(r, false, "Harga harus angka lebih dari 0"); return; }
    for (auto& h : T["H"]) if (h[0] == P(q, "key")) { h[1] = P(q, "val"); save(); out(r, true); return; }
    out(r, false, "Kunci harga tidak dikenal");
  });
  printf("INDI GOO berjalan di http://localhost:8080 (tekan Ctrl+C untuk berhenti)\n");
  svr.listen("127.0.0.1", 8080);
}
