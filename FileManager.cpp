// FileManager.cpp – lightweight LittleFS file browser for Bewaesserung
// Adapted from icebear74/Panelclock FileManager.cpp (simplified, no PsramVector)

#include "FileManager.h"
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ─── Module-level server pointer ─────────────────────────────────────────────

static WebServer* _fmServer = nullptr;

// ─── PSRAM allocator ─────────────────────────────────────────────────────────

struct FmPsramAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : malloc(size);
    }
    void deallocate(void* pointer) override { free(pointer); }
    void* reallocate(void* ptr, size_t new_size) override {
        void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : realloc(ptr, new_size);
    }
};

// ─── Path helpers ─────────────────────────────────────────────────────────────

static String fmSanitize(const String& raw) {
    if (raw.length() == 0) return "/";
    String p = raw;
    if (p.charAt(0) != '/') p = "/" + p;
    while (p.indexOf("..") != -1) p.replace("..", "");
    while (p.indexOf("//") != -1) p.replace("//", "/");
    while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
    if (p.length() == 0) return "/";
    return p;
}

static String fmParent(const String& path) {
    if (path == "/" || path.length() == 0) return "/";
    String p = path;
    if (p.endsWith("/") && p.length() > 1) p.remove(p.length() - 1);
    int idx = p.lastIndexOf('/');
    if (idx <= 0) return "/";
    return p.substring(0, idx);
}

static const char* fmContentType(const String& path) {
    String p = path;
    p.toLowerCase();
    if (p.endsWith(".htm") || p.endsWith(".html")) return "text/html";
    if (p.endsWith(".css"))  return "text/css";
    if (p.endsWith(".js"))   return "application/javascript";
    if (p.endsWith(".json")) return "application/json";
    if (p.endsWith(".png"))  return "image/png";
    if (p.endsWith(".jpg") || p.endsWith(".jpeg")) return "image/jpeg";
    if (p.endsWith(".txt"))  return "text/plain";
    return "application/octet-stream";
}

static String fmHumanSize(size_t bytes) {
    char buf[32];
    if (bytes < 1024) snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
    else if (bytes < 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0f);
    else snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0f * 1024.0f));
    return String(buf);
}

// Ensure parent directories of fullpath exist (creates recursively)
static void fmEnsureParentDirs(const String& fullpath) {
    int last = fullpath.lastIndexOf('/');
    if (last <= 0) return;
    String dir = fullpath.substring(0, last);
    if (dir.length() == 0 || dir == "/") return;
    if (!dir.startsWith("/")) dir = "/" + dir;
    String accum;
    int pos = 1;
    while (pos <= (int)dir.length()) {
        int next = dir.indexOf('/', pos);
        String token;
        if (next == -1) { token = dir.substring(pos); pos = dir.length() + 1; }
        else            { token = dir.substring(pos, next); pos = next + 1; }
        if (token.length() == 0) continue;
        accum += "/" + token;
        if (!LittleFS.exists(accum)) LittleFS.mkdir(accum);
    }
}

// ─── URL-decode helper ────────────────────────────────────────────────────────

static String fmUrlDecode(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        char c = in[i];
        if (c == '+') { out += ' '; }
        else if (c == '%' && i + 2 < in.length()) {
            auto hv = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'A' && h <= 'F') return 10 + (h - 'A');
                if (h >= 'a' && h <= 'f') return 10 + (h - 'a');
                return 0;
            };
            out += (char)((hv(in[i+1]) << 4) | hv(in[i+2]));
            i += 2;
        } else { out += c; }
    }
    return out;
}

// ─── GET /fs – Self-contained HTML UI ────────────────────────────────────────

static void handleFsUi() {
    String html;
    html.reserve(8000);
    html += F("<!doctype html><html><head>"
              "<meta charset='utf-8'/>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
              "<title>Bewässerung – Dateiverwaltung</title>"
              "<style>"
              "body{font-family:Arial,sans-serif;background:#f5f5f5;color:#333;margin:0}"
              ".navbar{background:#1a6b3c;padding:10px 16px;display:flex;align-items:center;flex-wrap:wrap;gap:8px}"
              ".navbar a{color:#fff;text-decoration:none;padding:6px 12px;border-radius:4px;font-size:14px;font-weight:bold}"
              ".navbar a:hover{background:rgba(255,255,255,.2)}"
              ".brand{color:#fff;font-size:18px;font-weight:bold;margin-right:8px}"
              ".container{max-width:1100px;margin:20px auto;padding:0 16px}"
              ".card{background:#fff;border-radius:8px;box-shadow:0 2px 6px rgba(0,0,0,.1);padding:20px;margin-bottom:16px}"
              "h1{font-size:20px;margin-bottom:12px;color:#1a6b3c}"
              ".fsinfo{font-size:13px;color:#555;margin-bottom:10px}"
              "table{width:100%;border-collapse:collapse}"
              "th,td{text-align:left;padding:8px 10px;border-bottom:1px solid #eee;font-size:13px}"
              "th{background:#f0f0f0;font-weight:bold}"
              ".btn{display:inline-block;padding:6px 12px;background:#1a6b3c;color:#fff;"
              "border:none;border-radius:4px;font-size:13px;cursor:pointer;text-decoration:none;margin-right:4px}"
              ".btn:hover{background:#145530}"
              ".btn-danger{background:#dc3545}.btn-danger:hover{background:#b02a37}"
              ".btn-secondary{background:#6c757d}.btn-secondary:hover{background:#545b62}"
              ".inp{padding:6px 8px;border:1px solid #ccc;border-radius:4px;font-size:13px}"
              ".cwd{font-size:13px;color:#555;margin-bottom:8px}"
              ".fdir{color:#1a6b3c;font-weight:bold}"
              ".ffile{color:#333}"
              "#msg{font-size:13px;color:#dc3545;min-height:18px;margin:6px 0}"
              ".ctrl{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-bottom:10px}"
              "</style></head><body>");
    html += F("<nav class='navbar'><span class='brand'>🌿 Bewässerung</span>"
              "<a href='/'>Status</a><a href='/backup'>Backup</a>"
              "<a href='/runlog'>Protokoll</a></nav>");
    html += F("<div class='container'><div class='card'>"
              "<h1>📁 Dateiverwaltung (LittleFS)</h1>"
              "<div class='fsinfo' id='fsinfo'>Lade Speicherinfo...</div>"
              "<div class='cwd' id='cwd'>Pfad: /</div>"
              "<div class='ctrl'>"
              "<button class='btn btn-secondary' onclick='list(currentPath)'>🔄 Aktualisieren</button>"
              "<form id='upForm' style='display:flex;gap:6px;align-items:center'>"
              "<input class='inp' id='fileInput' type='file'/>"
              "<input class='inp' id='destInput' type='text' placeholder='/optional/pfad' style='width:180px'/>"
              "<label style='font-size:13px'><input type='checkbox' id='overwrite'/> Überschreiben</label>"
              "<button class='btn' type='submit'>⬆ Upload</button></form>"
              "<input class='inp' id='newDir' type='text' placeholder='Neues Verzeichnis'/>"
              "<button class='btn' onclick='mkDir()'>📁 Erstellen</button></div>"
              "<div id='msg'></div>"
              "<table><thead><tr><th>Name</th><th>Größe</th><th>Aktionen</th></tr></thead>"
              "<tbody id='listing'></tbody></table></div></div>");
    html += F("<script>\nvar currentPath='/';\n"
              "async function refreshInfo(){\n"
              "  try{const r=await fetch('/fs/info');if(!r.ok)return;\n"
              "  const d=await r.json();\n"
              "  document.getElementById('fsinfo').innerText='Speicher: '+d.used_readable+' belegt / '+d.total_readable+' gesamt (frei: '+d.free_readable+')';\n"
              "  }catch(e){}\n"
              "}\n"
              "async function list(path){\n"
              "  currentPath=path||'/';\n"
              "  document.getElementById('cwd').innerText='Pfad: '+currentPath;\n"
              "  refreshInfo();\n"
              "  const r=await fetch('/fs/list?path='+encodeURIComponent(currentPath));\n"
              "  if(!r.ok){document.getElementById('msg').innerText='Fehler beim Laden: '+r.status;return;}\n"
              "  const j=await r.json();\n"
              "  const tb=document.querySelector('#listing');\n"
              "  tb.innerHTML='';\n"
              "  if(currentPath!=='/'){\n"
              "    const parent=currentPath.replace(/\\/[^\\/]*$/,'')||'/';\n"
              "    tb.insertAdjacentHTML('beforeend','<tr><td><a class=\"fdir\" href=\"#\" onclick=\"list(\\''+parent+'\\');return false\">..</a></td><td>–</td><td></td></tr>');\n"
              "  }\n"
              "  (j.entries||[]).forEach(function(e){\n"
              "    const cls=e.isDir?'fdir':'ffile';\n"
              "    const nameLink=e.isDir\n"
              "      ?'<a class=\"'+cls+'\" href=\"#\" onclick=\"list(\\''+e.path+'\\');return false\">'+e.name+'</a>'\n"
              "      :'<a class=\"'+cls+'\" href=\"#\" onclick=\"window.open(\\'/fs/download?path='+encodeURIComponent(e.path)+'\\');return false\">'+e.name+'</a>';\n"
              "    const sz=e.isDir?'–':(e.size_readable||'–');\n"
              "    let acts='';\n"
              "    if(!e.isDir){\n"
              "      acts+='<a class=\"btn\" href=\"/fs/download?path='+encodeURIComponent(e.path)+'\">↓</a> ';\n"
              "    } else {\n"
              "      acts+='<a class=\"btn\" href=\"#\" onclick=\"list(\\''+e.path+'\\');return false\">Öffnen</a> ';\n"
              "    }\n"
              "    acts+='<a class=\"btn\" href=\"#\" onclick=\"renameEntry(\\''+e.path+'\\');return false\">Umbenennen</a> ';\n"
              "    acts+='<a class=\"btn btn-danger\" href=\"#\" onclick=\"deleteEntry(\\''+e.path+'\\','+e.isDir+');return false\">Löschen</a>';\n"
              "    tb.insertAdjacentHTML('beforeend','<tr><td>'+nameLink+'</td><td>'+sz+'</td><td>'+acts+'</td></tr>');\n"
              "  });\n"
              "}\n"
              "async function deleteEntry(path,isDir){\n"
              "  if(!confirm((isDir?'Verzeichnis':'Datei')+' löschen: '+path+'?'))return;\n"
              "  const r=await fetch('/fs/delete?path='+encodeURIComponent(path),{method:'DELETE'});\n"
              "  const msg=document.getElementById('msg');\n"
              "  if(r.ok){msg.innerText='Gelöscht: '+path;list(currentPath);refreshInfo();}\n"
              "  else{const t=await r.text();msg.innerText='Fehler: '+r.status+' '+t;}\n"
              "}\n"
              "async function renameEntry(path){\n"
              "  const base=path.substring(path.lastIndexOf('/')+1);\n"
              "  const input=prompt('Neuer Name oder Pfad:',base);\n"
              "  if(input===null)return;\n"
              "  const p=new URLSearchParams();p.append('src',path);p.append('dest',input);p.append('cwd',currentPath);\n"
              "  const r=await fetch('/fs/rename?'+p.toString());\n"
              "  const msg=document.getElementById('msg');\n"
              "  if(r.ok){msg.innerText='Umbenannt';list(currentPath);}\n"
              "  else{const t=await r.text();msg.innerText='Fehler: '+r.status+' '+t;}\n"
              "}\n"
              "async function mkDir(){\n"
              "  const name=document.getElementById('newDir').value.trim();\n"
              "  if(!name){document.getElementById('msg').innerText='Bitte Namen eingeben';return;}\n"
              "  const p=new URLSearchParams();p.append('path',currentPath);p.append('name',name);\n"
              "  const r=await fetch('/fs/mkdir?'+p.toString());\n"
              "  const msg=document.getElementById('msg');\n"
              "  if(r.ok){document.getElementById('newDir').value='';msg.innerText='Verzeichnis erstellt';list(currentPath);refreshInfo();}\n"
              "  else{const t=await r.text();msg.innerText='Fehler: '+r.status+' '+t;}\n"
              "}\n"
              "document.getElementById('upForm').onsubmit=async function(ev){\n"
              "  ev.preventDefault();\n"
              "  const fi=document.getElementById('fileInput');\n"
              "  if(!fi.files.length){document.getElementById('msg').innerText='Keine Datei ausgewählt';return;}\n"
              "  const fd=new FormData();fd.append('file',fi.files[0]);\n"
              "  const dest=document.getElementById('destInput').value;\n"
              "  const ow=document.getElementById('overwrite').checked?'1':'0';\n"
              "  const p=new URLSearchParams();\n"
              "  if(dest)p.append('dest',dest);else p.append('dest',currentPath);\n"
              "  p.append('cwd',currentPath);p.append('overwrite',ow);\n"
              "  document.getElementById('msg').innerText='Hochladen...';\n"
              "  const r=await fetch('/fs/upload?'+p.toString(),{method:'POST',body:fd});\n"
              "  if(r.ok){document.getElementById('msg').innerText='Upload abgeschlossen';list(currentPath);refreshInfo();}\n"
              "  else document.getElementById('msg').innerText='Upload fehlgeschlagen: '+r.status;\n"
              "};\n"
              "list('/');refreshInfo();\n"
              "</script></body></html>");
    _fmServer->send(200, "text/html; charset=UTF-8", html);
}

// ─── GET /fs/info ─────────────────────────────────────────────────────────────

static void handleFsInfo() {
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    size_t free_ = total - used;
    FmPsramAllocator alloc;
    JsonDocument doc(&alloc);
    doc["total_bytes"]    = total;
    doc["used_bytes"]     = used;
    doc["free_bytes"]     = free_;
    doc["total_readable"] = fmHumanSize(total);
    doc["used_readable"]  = fmHumanSize(used);
    doc["free_readable"]  = fmHumanSize(free_);
    String out;
    serializeJson(doc, out);
    _fmServer->send(200, "application/json", out);
}

// ─── GET /fs/list ─────────────────────────────────────────────────────────────

static void handleFsList() {
    String raw  = _fmServer->hasArg("path") ? _fmServer->arg("path") : "/";
    String path = fmSanitize(raw);

    FmPsramAllocator alloc;
    JsonDocument doc(&alloc);
    doc["path"] = path;
    JsonArray arr = doc["entries"].to<JsonArray>();

    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) {
        _fmServer->send(400, "application/json", "{\"error\":\"invalid_path\"}");
        return;
    }
    File file = dir.openNextFile();
    while (file) {
        JsonObject e   = arr.add<JsonObject>();
        bool      isDir = file.isDirectory();

        // Build normalized absolute path from the raw name returned by the FS
        String rawName = file.name();
        String fullPath;
        if (rawName.startsWith("/")) {
            fullPath = rawName;
        } else if (rawName.indexOf('/') != -1) {
            fullPath = "/" + rawName;
        } else {
            fullPath = (path == "/") ? "/" + rawName : path + "/" + rawName;
        }
        String displayName = fullPath.substring(fullPath.lastIndexOf('/') + 1);

        e["name"]  = displayName;
        e["path"]  = fullPath;
        e["isDir"] = isDir;
        if (!isDir) {
            size_t sz = file.size();
            e["size_bytes"]    = sz;
            e["size_readable"] = fmHumanSize(sz);
        }
        file.close();
        file = dir.openNextFile();
    }
    String out;
    serializeJson(doc, out);
    _fmServer->send(200, "application/json", out);
}

// ─── GET /fs/download ────────────────────────────────────────────────────────

static void handleFsDownload() {
    if (!_fmServer->hasArg("path")) {
        _fmServer->send(400, "text/plain", "missing path");
        return;
    }
    String path = fmSanitize(_fmServer->arg("path"));
    if (!LittleFS.exists(path)) {
        _fmServer->send(404, "text/plain", "not found");
        return;
    }
    File f = LittleFS.open(path, "r");
    if (!f) { _fmServer->send(500, "text/plain", "open failed"); return; }
    String fname = path.substring(path.lastIndexOf('/') + 1);
    _fmServer->sendHeader("Content-Disposition",
                          "attachment; filename=\"" + fname + "\"");
    _fmServer->streamFile(f, fmContentType(path));
    f.close();
}

// ─── DELETE /fs/delete ───────────────────────────────────────────────────────

static void handleFsDelete() {
    if (!_fmServer->hasArg("path")) {
        _fmServer->send(400, "text/plain", "missing path");
        return;
    }
    String path = fmSanitize(_fmServer->arg("path"));
    if (!LittleFS.exists(path)) {
        _fmServer->send(404, "text/plain", "not found");
        return;
    }
    File f = LittleFS.open(path);
    bool isDir = f && f.isDirectory();
    if (f) f.close();

    if (isDir) {
        // Only allow deletion of empty directories
        File d = LittleFS.open(path);
        bool empty = true;
        if (d) {
            File child = d.openNextFile();
            if (child) { empty = false; child.close(); }
            d.close();
        }
        if (!empty) {
            _fmServer->send(409, "text/plain", "directory_not_empty");
            return;
        }
        if (!LittleFS.remove(path)) {
            _fmServer->send(500, "text/plain", "rmdir_failed");
            return;
        }
    } else {
        if (!LittleFS.remove(path)) {
            _fmServer->send(500, "text/plain", "remove_failed");
            return;
        }
    }
    _fmServer->send(200, "application/json", "{\"success\":true}");
    Serial.printf("[FM] Deleted: %s\n", path.c_str());
}

// ─── GET /fs/mkdir ───────────────────────────────────────────────────────────

static void handleFsMkdir() {
    String parent = _fmServer->hasArg("path") ? _fmServer->arg("path") : "/";
    String name   = _fmServer->hasArg("name") ? _fmServer->arg("name") : "";
    parent = fmSanitize(parent);
    if (name.length() == 0)            { _fmServer->send(400, "text/plain", "missing name");  return; }
    if (name.indexOf('/') != -1 ||
        name.indexOf("..") != -1)      { _fmServer->send(400, "text/plain", "invalid name");  return; }

    String target = (parent == "/" ? "/" : parent + "/") + name;
    target = fmSanitize(target);
    if (LittleFS.exists(target))       { _fmServer->send(409, "text/plain", "already_exists"); return; }
    if (!LittleFS.mkdir(target))       { _fmServer->send(500, "text/plain", "mkdir_failed");   return; }
    _fmServer->send(200, "application/json", "{\"success\":true}");
    Serial.printf("[FM] mkdir: %s\n", target.c_str());
}

// ─── GET /fs/rename ──────────────────────────────────────────────────────────

static void handleFsRename() {
    String src  = _fmServer->hasArg("src")  ? fmUrlDecode(_fmServer->arg("src"))  : "";
    String dest = _fmServer->hasArg("dest") ? fmUrlDecode(_fmServer->arg("dest")) : "";
    String cwd  = _fmServer->hasArg("cwd")  ? fmUrlDecode(_fmServer->arg("cwd"))  : "/";

    if (src.length() == 0 || dest.length() == 0) {
        _fmServer->send(400, "text/plain", "missing src or dest");
        return;
    }
    // Sanitize source
    src = fmSanitize(src);

    // If dest has no leading slash, treat it as relative to cwd
    if (dest.indexOf('/') == -1 && !dest.startsWith("/")) {
        String base = fmSanitize(cwd);
        dest = (base == "/" ? "/" : base + "/") + dest;
    }
    dest = fmSanitize(dest);

    if (src.indexOf("..") != -1 || dest.indexOf("..") != -1) {
        _fmServer->send(400, "text/plain", "invalid path");
        return;
    }
    if (!LittleFS.exists(src)) {
        _fmServer->send(404, "text/plain", "source not found");
        return;
    }
    if (LittleFS.exists(dest)) {
        _fmServer->send(409, "text/plain", "destination exists");
        return;
    }
    fmEnsureParentDirs(dest);
    if (!LittleFS.rename(src, dest)) {
        _fmServer->send(500, "text/plain", "rename failed");
        return;
    }
    _fmServer->send(200, "application/json", "{\"success\":true}");
    Serial.printf("[FM] Renamed: %s -> %s\n", src.c_str(), dest.c_str());
}

// ─── POST /fs/upload ─────────────────────────────────────────────────────────

static File     _fsUploadFile;
static String   _fsUploadTarget;
static String   _fsUploadTmp;

static void handleFsUploadBegin() {
    _fmServer->send(200, "application/json", "{\"success\":true}");
}

static void handleFsUploadChunk() {
    if (!_fmServer) return;
    // Guard: only handle if this is /fs/upload
    if (_fmServer->uri() != "/fs/upload") return;

    HTTPUpload& upload = _fmServer->upload();

    if (upload.status == UPLOAD_FILE_START) {
        String dest     = _fmServer->arg("dest");
        String cwd      = _fmServer->arg("cwd");
        String filename = upload.filename;
        bool overwrite  = (_fmServer->arg("overwrite") == "1");

        if (dest.length() == 0) dest = cwd.length() ? cwd : "/";
        dest = fmSanitize(dest);

        // If dest is a directory path (ends with / or it exists as a directory),
        // append the uploaded filename.
        if (LittleFS.exists(dest)) {
            File f = LittleFS.open(dest);
            bool isDir = f && f.isDirectory();
            if (f) f.close();
            if (isDir) dest = (dest == "/" ? "/" : dest + "/") + filename;
        } else if (dest.endsWith("/")) {
            dest += filename;
        } else if (dest.indexOf('.') == -1) {
            // Likely a directory path without trailing slash
            dest = dest + "/" + filename;
        }
        dest = fmSanitize(dest);

        _fsUploadTarget = dest;
        _fsUploadTmp    = dest + ".tmp";

        if (LittleFS.exists(_fsUploadTarget) && !overwrite) {
            _fsUploadFile = File();
            Serial.printf("[FM] Upload refused (exists, no overwrite): %s\n", _fsUploadTarget.c_str());
            return;
        }
        fmEnsureParentDirs(_fsUploadTarget);
        if (LittleFS.exists(_fsUploadTmp)) LittleFS.remove(_fsUploadTmp);
        _fsUploadFile = LittleFS.open(_fsUploadTmp, "w");
        if (!_fsUploadFile) Serial.printf("[FM] Upload start: cannot open tmp %s\n", _fsUploadTmp.c_str());
        else Serial.printf("[FM] Upload start -> %s (tmp: %s)\n", _fsUploadTarget.c_str(), _fsUploadTmp.c_str());

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_fsUploadFile) _fsUploadFile.write(upload.buf, upload.currentSize);

    } else if (upload.status == UPLOAD_FILE_END) {
        if (_fsUploadFile) {
            _fsUploadFile.close();
            if (LittleFS.exists(_fsUploadTarget)) LittleFS.remove(_fsUploadTarget);
            bool moved = LittleFS.rename(_fsUploadTmp, _fsUploadTarget);
            if (!moved) {
                // Fallback: manual copy — preserve temp file on any write error
                File src = LittleFS.open(_fsUploadTmp, "r");
                File dst = LittleFS.open(_fsUploadTarget, "w");
                bool copyOk = (src && dst);
                if (copyOk) {
                    uint8_t buf[512];
                    while (src.available()) {
                        size_t n = src.readBytes((char*)buf, sizeof(buf));
                        if (dst.write(buf, n) != n) { copyOk = false; break; }
                    }
                }
                if (src) src.close();
                if (dst) dst.close();
                if (copyOk) {
                    LittleFS.remove(_fsUploadTmp);
                    moved = true;
                } else {
                    // Partial write: remove the corrupt target, keep temp for diagnosis
                    LittleFS.remove(_fsUploadTarget);
                    Serial.printf("[FM] Fallback copy FAILED for %s; temp kept at %s\n",
                                  _fsUploadTarget.c_str(), _fsUploadTmp.c_str());
                }
            }
            Serial.printf("[FM] Upload %s -> %s (%u bytes)\n",
                          moved ? "done" : "FAILED",
                          _fsUploadTarget.c_str(), (unsigned)upload.totalSize);
        }

    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (_fsUploadFile) { _fsUploadFile.close(); LittleFS.remove(_fsUploadTmp); }
        Serial.println("[FM] Upload aborted");
    }
}

// ─── Route registration ───────────────────────────────────────────────────────

void setupFileManagerRoutes(WebServer* server) {
    _fmServer = server;
    server->on("/fs",           HTTP_GET,    handleFsUi);
    server->on("/fs/info",      HTTP_GET,    handleFsInfo);
    server->on("/fs/list",      HTTP_GET,    handleFsList);
    server->on("/fs/download",  HTTP_GET,    handleFsDownload);
    server->on("/fs/delete",    HTTP_DELETE, handleFsDelete);
    server->on("/fs/upload",    HTTP_POST,   handleFsUploadBegin, handleFsUploadChunk);
    server->on("/fs/mkdir",     HTTP_GET,    handleFsMkdir);
    server->on("/fs/rename",    HTTP_GET,    handleFsRename);
    Serial.println("[FM] File manager routes registered at /fs");
}
