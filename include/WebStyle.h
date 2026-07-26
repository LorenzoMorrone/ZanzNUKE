#pragma once

#include <Arduino.h>


/*
 * ---------------------------------------------------------
 * Shared mobile-first page shell, used by every HTML page in
 * both WebServerManager (dashboard/config/schedule/...) and
 * NetworkManager's WiFi setup server, so the whole app looks
 * and behaves consistently regardless of which server is
 * generating the page.
 *
 * Usage:
 *   String html = htmlHead("Page title");
 *   html += "<h1>Page title</h1>";
 *   ... page content ...
 *   html += htmlFoot("config");   // bottom tab bar, "config" active
 *   // or htmlFoot() with no args for a standalone page (no tabs,
 *   // e.g. the WiFi setup page, whose server only has 1-2 routes)
 * ---------------------------------------------------------
 */

inline String htmlHead(const char* title)
{

    String html = "<!DOCTYPE html><html><head><title>";

    html += title;

    html += R"rawliteral(</title>
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<meta name="theme-color" content="#0284c7">
<style>
:root{
  --bg:#f2f4f7; --card:#ffffff; --text:#12161b; --muted:#667085; --border:#e3e7ec;
  --primary:#0284c7; --primary-d:#026ea3; --success:#16a34a; --danger:#dc2626; --warning:#b45309;
  --success-bg:rgba(22,163,74,.12); --danger-bg:rgba(220,38,38,.12); --primary-bg:rgba(2,132,199,.12);
  --radius:16px;
}
@media (prefers-color-scheme:dark){
  :root{
    --bg:#0d0f12; --card:#181b20; --text:#eef1f4; --muted:#8b95a1; --border:#2a2e35;
    --primary:#38bdf8; --primary-d:#0ea5e9; --success:#22c55e; --danger:#f87171; --warning:#fbbf24;
    --success-bg:rgba(34,197,94,.15); --danger-bg:rgba(248,113,113,.15); --primary-bg:rgba(56,189,248,.15);
  }
}
*{box-sizing:border-box; -webkit-tap-highlight-color:transparent}
body{
  margin:0; padding:18px 16px 100px; background:var(--bg); color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  font-size:16px; line-height:1.5; max-width:520px; margin-left:auto; margin-right:auto;
}
h1{font-size:21px; margin:2px 0 16px; font-weight:700}
h3{font-size:12px; margin:0 0 12px; text-transform:uppercase; letter-spacing:.05em; color:var(--muted); font-weight:700}
.card{background:var(--card); border:1px solid var(--border); border-radius:var(--radius); padding:18px; margin:0 0 16px; box-shadow:0 1px 3px rgba(0,0,0,.05)}
.card + button, .card + .btn-row{margin-top:-4px}
label{display:block; margin-top:14px; margin-bottom:5px; font-size:13px; color:var(--muted)}
label:first-child{margin-top:0}
input[type=text],input[type=number],input[type=password],input[type=time],select{
  width:100%; padding:12px 13px; font-size:16px; border-radius:10px; border:1px solid var(--border);
  background:var(--bg); color:var(--text); appearance:none; -webkit-appearance:none;
}
input:not([type=checkbox]),select{width:100%}
input[type=checkbox]{width:20px; height:20px; accent-color:var(--primary)}
input[type=range]{width:100%; accent-color:var(--primary); padding:0; margin-top:4px}
details{margin:0 0 16px}
details summary{
  cursor:pointer; font-size:13px; font-weight:700; color:var(--muted); padding:14px 18px;
  background:var(--card); border:1px solid var(--border); border-radius:var(--radius);
  list-style:none; text-transform:uppercase; letter-spacing:.05em;
}
details summary::-webkit-details-marker{display:none}
details summary::after{content:'\25BE'; float:right; text-transform:none; letter-spacing:0}
details[open] summary::after{content:'\25B4'}
details .card{margin-top:16px}
input:focus,select:focus{outline:2px solid var(--primary); outline-offset:1px}
button{
  display:block; width:100%; padding:15px; margin:10px 0 0; font-size:16px; font-weight:600;
  border-radius:12px; border:none; background:var(--primary); color:#fff; cursor:pointer;
}
button:active{opacity:.85}
button:disabled{opacity:.5}
.btn-secondary{background:var(--card); color:var(--text); border:1px solid var(--border)}
.btn-danger{background:var(--danger)}
.btn-warning{background:var(--warning)}
.btn-row{display:flex; gap:10px}
.btn-row button{margin-top:10px}
a.btn{display:block; text-align:center; text-decoration:none; box-sizing:border-box}
.err{color:var(--danger); font-weight:600}
.ok{color:var(--success); font-weight:600}
.hint{font-size:12px; color:var(--muted); margin-top:8px; line-height:1.4}
pre{white-space:pre-wrap; word-break:break-word; font-size:13.5px; margin:0; font-family:ui-monospace,Menlo,Consolas,monospace}
.badge{display:inline-block; padding:7px 16px; border-radius:999px; font-weight:700; font-size:14px}
.badge-idle{background:var(--success-bg); color:var(--success)}
.badge-active{background:var(--primary-bg); color:var(--primary)}
.badge-error{background:var(--danger-bg); color:var(--danger)}
.stat-grid{display:grid; grid-template-columns:1fr 1fr; gap:14px; margin-top:18px; text-align:left}
.stat-label{font-size:11.5px; color:var(--muted); display:block; margin-bottom:2px}
.stat-value{font-size:15px; font-weight:600}
.center{text-align:center}
.row{display:flex; align-items:center; gap:12px}
.row input[type=time]{flex:1}
.daycard .row{margin-top:10px}
.daycard .row:first-of-type{margin-top:2px}
nav.tabbar{
  position:fixed; left:0; right:0; bottom:0; display:flex; background:var(--card);
  border-top:1px solid var(--border); padding:6px 4px calc(6px + env(safe-area-inset-bottom));
  max-width:520px; margin:0 auto;
}
nav.tabbar a{
  flex:1; text-align:center; text-decoration:none; color:var(--muted); font-size:10.5px;
  padding:6px 2px; border-radius:10px; font-weight:600;
}
nav.tabbar a .ic{display:block; font-size:20px; line-height:1.3}
nav.tabbar a.active{color:var(--primary); background:var(--primary-bg)}
</style>
</head><body>
)rawliteral";

    return html;

}



struct NavTab
{
    const char* key;
    const char* href;
    const char* icon;
    const char* label;
};


inline String htmlFoot(const char* activeKey = nullptr)
{

    String html;

    if(activeKey != nullptr)
    {

        static const NavTab tabs[] = {
            { "home",       "/",             "&#127968;", "Home" },
            { "config",     "/config",       "&#9881;",   "Config" },
            { "schedule",   "/schedule",     "&#128197;", "Schedule" },
            { "diagnostics","/diagnostics",  "&#128202;", "Status" },
            { "test",       "/test",         "&#129514;", "Test" }
        };

        html += "<nav class='tabbar'>";

        for(const NavTab &t : tabs)
        {

            html += "<a href='";
            html += t.href;
            html += "'";

            if(strcmp(activeKey, t.key) == 0)
                html += " class='active'";

            html += "><span class='ic'>";
            html += t.icon;
            html += "</span>";
            html += t.label;
            html += "</a>";

        }

        html += "</nav>";

    }

    html += "</body></html>";

    return html;

}
