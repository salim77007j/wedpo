//! Wedpo privacy core — the Rust brain of the Wedpo browser.
//!
//! Responsibilities:
//! - Network filter engine (EasyList/EasyPrivacy syntax, via the `adblock` crate)
//! - Cosmetic filter CSS generation (specific rules per URL + generic class/id rules)
//! - Tracking-parameter stripping (utm_*, fbclid, gclid, ...)
//! - Download risk scoring (executable/double-extension/shortener heuristics)
//!
//! Exposed to the Qt shell over a small, stable C ABI.

use std::collections::BTreeMap;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::sync::Mutex;

use adblock::lists::{FilterSet, ParseOptions};
use adblock::Engine;
use url::Url;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

/// Parameters known to exist purely for cross-site tracking / marketing attribution.
const TRACKING_PARAMS: &[&str] = &[
    "fbclid", "gclid", "dclid", "msclkid", "mc_eid", "igshid", "_hsenc", "_hsmi",
    "vero_id", "vero_conv", "wickedid", "wickedsource", "yclid", "s_kwcid", "ttclid",
    "twclid", "trkid", "ref_src", "ref_url", "spm", "scm", "oly_anon_id", "oly_enc_id",
    "rb_clickid", "wt.mc_id", "campaign_id", "ad_id",
    "utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content", "utm_id",
];

/// File extensions that mean "this is code the OS may execute".
const EXECUTABLE_EXTS: &[&str] = &[
    "exe", "msi", "scr", "com", "pif", "hta", "bat", "cmd", "vbs", "vbe", "js", "jse",
    "ps1", "psm1", "jar", "apk", "dll", "lnk", "gadget",
];

/// Macro-enabled Office formats (can carry scripts).
const MACRO_EXTS: &[&str] = &["docm", "xlsm", "pptm", "xlam", "dotm"];

/// Archive / OS image formats.
const ARCHIVE_EXTS: &[&str] = &["zip", "rar", "7z", "iso", "img", "gz", "tar", "cab"];

/// Popular link shorteners.
const SHORTENERS: &[&str] = &[
    "bit.ly", "t.co", "tinyurl.com", "cutt.ly", "is.gd", "rb.gy", "rebrand.ly",
    "ow.ly", "shorturl.at", "tiny.cc", "bit.do", "s.id", "shrtco.de", "gg.gg",
];

struct State {
    lists: BTreeMap<String, String>,
    engine: Engine,
}

impl State {
    fn new() -> Self {
        let mut st = State {
            lists: BTreeMap::new(),
            engine: Engine::new_with_filter_set(FilterSet::new(false)),
        };
        st.rebuild();
        st
    }

    fn rebuild(&mut self) {
        let mut fs = FilterSet::new(false);
        for text in self.lists.values() {
            fs.add_filter_list(text.clone(), ParseOptions::default());
        }
        self.engine = Engine::new_with_filter_set(fs);
    }
}

// The `adblock` 0.13 Engine is neither Send nor Sync (Rc-based internals), so all
// engine access is funneled through one dedicated worker thread. The C ABI stays
// synchronous: each FFI call enqueues a job and blocks for the reply.
enum Job {
    LoadList { name: String, text: String },
    RemoveList { name: String },
    Check { url: String, rtype: String, first_party_host: String },
    Cosmetic { url: String },
    GenericCss { classes: Vec<String>, ids: Vec<String>, exceptions: Vec<String> },
    StripTracking { url: String },
    Risk { url: String, mime: String },
    Info,
}

enum Reply {
    Int(i32),
    Str(String),
    Risk(i32, String),
}

fn handle_job(state: &mut State, job: Job) -> Reply {
    match job {
        Job::LoadList { name, text } => {
            if name.is_empty() || text.trim().is_empty() {
                Reply::Int(-1)
            } else {
                state.lists.insert(name, text);
                state.rebuild();
                Reply::Int(1)
            }
        }
        Job::RemoveList { name } => {
            if state.lists.remove(&name).is_some() {
                state.rebuild();
                Reply::Int(1)
            } else {
                Reply::Int(0)
            }
        }
        Job::Check { url, rtype, first_party_host } => {
            let source_url = if first_party_host.is_empty() {
                String::new()
            } else {
                format!("https://{}/", first_party_host)
            };
            let result = match adblock::request::Request::new(&url, &source_url, &rtype, "get") {
                Ok(r) => state.engine.check_network_request(&r),
                Err(_) => return Reply::Int(0),
            };
            if let Some(redir) = &result.redirect {
                if !redir.is_empty() && redir.starts_with("data:") {
                    return Reply::Str(redir.clone());
                }
            }
            Reply::Int(if result.should_block() { 1 } else { 0 })
        }
        Job::Cosmetic { url } => {
            let res = state.engine.url_cosmetic_resources(&url);
            let mut css = String::new();
            if !res.generichide {
                for sel in &res.hide_selectors {
                    if !res.exceptions.contains(sel) {
                        push_hide_rule(&mut css, sel);
                    }
                }
            }
            Reply::Str(css)
        }
        Job::GenericCss { classes, ids, exceptions } => {
            let exc: std::collections::HashSet<String> = exceptions.into_iter().collect();
            let selectors = state.engine.hidden_class_id_selectors(classes.iter(), ids.iter(), &exc);
            let mut css = String::new();
            for sel in selectors {
                push_hide_rule(&mut css, &sel);
            }
            Reply::Str(css)
        }
        Job::StripTracking { url } => match strip_tracking_impl(&url) {
            Some(clean) => Reply::Str(clean),
            None => Reply::Str(url),
        },
        Job::Risk { url, mime } => {
            let (score, reasons) = download_risk_impl(&url, &mime);
            let json = format!(
                "[{}]",
                reasons.iter().map(|r| format!("\"{}\"", json_escape(r))).collect::<Vec<_>>().join(",")
            );
            Reply::Risk(score, json)
        }
        Job::Info => Reply::Str(lists_info_impl(state)),
    }
}

pub struct WedpoEngine {
    tx: Mutex<std::sync::mpsc::Sender<(Job, std::sync::mpsc::Sender<Reply>)>>,
}

impl Default for WedpoEngine {
    fn default() -> Self {
        Self::new()
    }
}

impl WedpoEngine {
    pub fn new() -> Self {
        let (tx, rx) = std::sync::mpsc::channel::<(Job, std::sync::mpsc::Sender<Reply>)>();
        std::thread::Builder::new()
            .name("wedpo-core".into())
            .spawn(move || {
                let mut state = State::new();
                while let Ok((job, resp)) = rx.recv() {
                    let reply = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                        handle_job(&mut state, job)
                    }))
                    .unwrap_or(Reply::Int(-1));
                    let _ = resp.send(reply);
                }
            })
            .expect("wedpo core worker thread");
        WedpoEngine { tx: Mutex::new(tx) }
    }

    fn submit(&self, job: Job) -> Option<Reply> {
        let (rtx, rrx) = std::sync::mpsc::channel();
        let tx = self.tx.lock().ok()?.clone();
        tx.send((job, rtx)).ok()?;
        rrx.recv().ok()
    }

    pub fn load_list(&self, name: &str, text: &str) -> i32 {
        match self.submit(Job::LoadList { name: name.to_string(), text: text.to_string() }) {
            Some(Reply::Int(v)) => v,
            _ => -1,
        }
    }

    pub fn remove_list(&self, name: &str) -> i32 {
        match self.submit(Job::RemoveList { name: name.to_string() }) {
            Some(Reply::Int(v)) => v,
            _ => -1,
        }
    }

    /// 0 = allow, 1 = block, 2 = redirect (redirect URL written into `redirect_out`).
    pub fn check(&self, url: &str, rtype: &str, first_party_host: &str, redirect_out: &mut String) -> i32 {
        match self.submit(Job::Check {
            url: url.to_string(),
            rtype: rtype.to_string(),
            first_party_host: first_party_host.to_string(),
        }) {
            Some(Reply::Str(redirect)) => {
                *redirect_out = redirect;
                2
            }
            Some(Reply::Int(v)) => v,
            _ => 0,
        }
    }

    /// URL-specific cosmetic CSS (safe to apply immediately on navigation).
    pub fn cosmetic_css(&self, url: &str) -> String {
        match self.submit(Job::Cosmetic { url: url.to_string() }) {
            Some(Reply::Str(css)) => css,
            _ => String::new(),
        }
    }

    /// Generic class/id cosmetic CSS, applied after the page reports its DOM classes/ids.
    pub fn generic_css(&self, classes: &[String], ids: &[String], exceptions: &[String]) -> String {
        match self.submit(Job::GenericCss {
            classes: classes.to_vec(),
            ids: ids.to_vec(),
            exceptions: exceptions.to_vec(),
        }) {
            Some(Reply::Str(css)) => css,
            _ => String::new(),
        }
    }

    pub fn strip_tracking(&self, url: &str) -> Option<String> {
        match self.submit(Job::StripTracking { url: url.to_string() }) {
            Some(Reply::Str(s)) => {
                if s == url {
                    None
                } else {
                    Some(s)
                }
            }
            _ => None,
        }
    }

    /// Risk score 0-100 with reasons for a prospective download.
    pub fn download_risk(&self, url: &str, mime: &str) -> (i32, Vec<String>) {
        match self.submit(Job::Risk { url: url.to_string(), mime: mime.to_string() }) {
            Some(Reply::Risk(score, json)) => (score, parse_reasons(&json)),
            _ => (0, Vec::new()),
        }
    }

    pub fn lists_info(&self) -> String {
        match self.submit(Job::Info) {
            Some(Reply::Str(s)) => s,
            _ => String::from("{\"lists\":[],\"count\":0}"),
        }
    }
}

fn parse_reasons(json: &str) -> Vec<String> {
    // Minimal JSON string-array parse of our own well-formed output.
    json.trim_matches(|c| c == '[' || c == ']')
        .split("\",\"")
        .filter_map(|p| {
            let p = p.trim().trim_matches('"');
            if p.is_empty() {
                None
            } else {
                Some(p.to_string())
            }
        })
        .collect()
}

fn strip_tracking_impl(url: &str) -> Option<String> {
    let mut parsed = Url::parse(url).ok()?;
    if parsed.scheme() != "http" && parsed.scheme() != "https" {
        return None;
    }
    let mut changed = false;
    let mut kept: Vec<(String, String)> = Vec::new();
    for (k, v) in parsed.query_pairs() {
        let kl = k.to_lowercase();
        if TRACKING_PARAMS.contains(&kl.as_str()) || kl.starts_with("utm_") {
            changed = true;
        } else {
            kept.push((k.into_owned(), v.into_owned()));
        }
    }
    if !changed {
        return None;
    }
    parsed.set_query(None);
    if !kept.is_empty() {
        let mut q = String::new();
        for (i, (k, v)) in kept.iter().enumerate() {
            if i > 0 {
                q.push('&');
            }
            q.push_str(&form_urlencode(k));
            q.push('=');
            q.push_str(&form_urlencode(v));
        }
        parsed.set_query(Some(&q));
    }
    Some(parsed.to_string())
}

fn download_risk_impl(url: &str, mime: &str) -> (i32, Vec<String>) {
    let mut score = 0i32;
    let mut reasons: Vec<String> = Vec::new();
    let parsed = match Url::parse(url) {
        Ok(u) => u,
        Err(_) => return (0, reasons),
    };
    let host = parsed.host_str().unwrap_or("").to_lowercase();
    let path = parsed.path().to_lowercase();
    let file = path.rsplit('/').next().unwrap_or("");
    let ext = file.rsplit('.').next().unwrap_or("");

    if parsed.scheme() == "http" {
        score += 10;
        reasons.push("Downloaded over an insecure connection (HTTP)".into());
    }
    if host.split('.').any(|l| l.starts_with("xn--")) {
        score += 25;
        reasons.push("Punycode domain — may imitate a well-known site".into());
    }
    if SHORTENERS.contains(&host.as_str()) {
        score += 20;
        reasons.push("Link shortener hides the real download location".into());
    }
    if EXECUTABLE_EXTS.contains(&ext) {
        score += 45;
        reasons.push("Executable file format".into());
        let stem = &file[..file.len().saturating_sub(ext.len() + 1)];
        if stem.contains('.') && stem.rsplit('.').next().map_or(false, |e| e.len() >= 2) {
            score += 25;
            reasons.push("Disguised file extension (double extension)".into());
        }
    }
    if MACRO_EXTS.contains(&ext) {
        score += 30;
        reasons.push("Office file with macro capability".into());
    }
    if ARCHIVE_EXTS.contains(&ext) {
        score += 10;
        reasons.push("Archive or OS image — inspect contents before running".into());
    }
    if !mime.is_empty() && mime.contains("text/html") && EXECUTABLE_EXTS.contains(&ext) {
        score += 20;
        reasons.push("Server content type does not match the file extension".into());
    }
    if score > 100 {
        score = 100;
    }
    (score, reasons)
}

fn lists_info_impl(state: &State) -> String {
    let mut out = String::from("{\"lists\":[");
    let mut first = true;
    for (name, text) in &state.lists {
        let rules = text
            .lines()
            .filter(|l| {
                let t = l.trim();
                !t.is_empty() && !t.starts_with('!') && !t.starts_with('[')
            })
            .count();
        if !first {
            out.push(',');
        }
        first = false;
        out.push_str(&format!(
            "{{\"name\":\"{}\",\"bytes\":{},\"rules\":{}}}",
            json_escape(name),
            text.len(),
            rules
        ));
    }
    out.push_str(&format!("],\"count\":{}}}", state.lists.len()));
    out
}

fn push_hide_rule(css: &mut String, selector: &str) {
    let sel = selector.trim();
    if sel.is_empty() || sel.contains('{') {
        return;
    }
    css.push_str(sel);
    css.push_str("{display:none!important;visibility:hidden!important;}");
}

fn form_urlencode(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => out.push(b as char),
            b' ' => out.push_str("%20"),
            _ => out.push_str(&format!("%{:02X}", b)),
        }
    }
    out
}

fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

// ---------------------------------------------------------------------------
// C ABI
// ---------------------------------------------------------------------------

unsafe fn read_str(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    CStr::from_ptr(p).to_string_lossy().into_owned()
}

unsafe fn write_out(out: *mut c_char, cap: c_int, s: &str) {
    if out.is_null() || cap <= 0 {
        return;
    }
    let cap = cap as usize;
    let bytes = s.as_bytes();
    let n = bytes.len().min(cap - 1);
    std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, n);
    *out.add(n) = 0;
}

#[no_mangle]
pub extern "C" fn wedpo_core_version() -> *const c_char {
    let v = CString::new(VERSION).unwrap();
    // Leak one small static string per process; version is queried once.
    static LEAK: std::sync::OnceLock<CString> = std::sync::OnceLock::new();
    LEAK.get_or_init(|| v).as_ptr()
}

#[no_mangle]
pub extern "C" fn wedpo_engine_new() -> *mut WedpoEngine {
    Box::into_raw(Box::new(WedpoEngine::new()))
}

/// # Safety
/// `e` must be a pointer returned by `wedpo_engine_new`, freed exactly once.
#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_free(e: *mut WedpoEngine) {
    if !e.is_null() {
        drop(Box::from_raw(e));
    }
}

/// Returns 1 on success, -1 on error.
#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_load_list(
    e: *mut WedpoEngine, name: *const c_char, text: *const c_char,
) -> c_int {
    match e.as_ref() {
        Some(e) => e.load_list(&read_str(name), &read_str(text)),
        None => -1,
    }
}

#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_remove_list(e: *mut WedpoEngine, name: *const c_char) -> c_int {
    match e.as_ref() {
        Some(e) => e.remove_list(&read_str(name)),
        None => -1,
    }
}

/// 0 = allow, 1 = block, 2 = redirect (URL in `redirect_out`), -1 on null engine.
#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_check(
    e: *mut WedpoEngine,
    url: *const c_char,
    rtype: *const c_char,
    first_party_host: *const c_char,
    redirect_out: *mut c_char,
    cap: c_int,
) -> c_int {
    let e = match e.as_ref() {
        Some(e) => e,
        None => return -1,
    };
    let mut redirect = String::new();
    let r = e.check(&read_str(url), &read_str(rtype), &read_str(first_party_host), &mut redirect);
    if r == 2 {
        write_out(redirect_out, cap, &redirect);
    }
    r
}

/// URL-specific cosmetic CSS for `url`; returns CSS byte length.
#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_cosmetic(
    e: *mut WedpoEngine, url: *const c_char, out: *mut c_char, cap: c_int,
) -> c_int {
    let e = match e.as_ref() {
        Some(e) => e,
        None => return -1,
    };
    let css = e.cosmetic_css(&read_str(url));
    write_out(out, cap, &css);
    css.len() as c_int
}

/// Generic CSS from page-reported classes/ids (newline separated). Returns CSS byte length.
#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_generic_css(
    e: *mut WedpoEngine,
    classes: *const c_char,
    ids: *const c_char,
    exceptions: *const c_char,
    out: *mut c_char,
    cap: c_int,
) -> c_int {
    let e = match e.as_ref() {
        Some(e) => e,
        None => return -1,
    };
    let split = |s: String| -> Vec<String> {
        s.split('\n').map(|x| x.trim().to_string()).filter(|x| !x.is_empty()).collect()
    };
    let classes = split(read_str(classes));
    let ids = split(read_str(ids));
    let exceptions = split(read_str(exceptions));
    let css = e.generic_css(&classes, &ids, &exceptions);
    write_out(out, cap, &css);
    css.len() as c_int
}

/// Strip tracking params. Returns 1 if rewritten (out = new URL), 0 unchanged, -1 on error.
#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_strip_tracking(
    e: *mut WedpoEngine, url: *const c_char, out: *mut c_char, cap: c_int,
) -> c_int {
    let e = match e.as_ref() {
        Some(e) => e,
        None => return -1,
    };
    let raw = read_str(url);
    match e.strip_tracking(&raw) {
        Some(clean) => {
            write_out(out, cap, &clean);
            1
        }
        None => {
            write_out(out, cap, &raw);
            0
        }
    }
}

/// Returns risk score 0-100 (JSON array reasons in `reasons_out`), -1 on error.
#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_risk(
    e: *mut WedpoEngine, url: *const c_char, mime: *const c_char, reasons_out: *mut c_char, cap: c_int,
) -> c_int {
    let e = match e.as_ref() {
        Some(e) => e,
        None => return -1,
    };
    let (score, reasons) = e.download_risk(&read_str(url), &read_str(mime));
    let json = format!(
        "[{}]",
        reasons.iter().map(|r| format!("\"{}\"", json_escape(r))).collect::<Vec<_>>().join(",")
    );
    write_out(reasons_out, cap, &json);
    score
}

/// Loaded lists as JSON in `out`; returns length.
#[no_mangle]
pub unsafe extern "C" fn wedpo_engine_lists_info(
    e: *mut WedpoEngine, out: *mut c_char, cap: c_int,
) -> c_int {
    match e.as_ref() {
        Some(e) => {
            let info = e.lists_info();
            write_out(out, cap, &info);
            info.len() as c_int
        }
        None => -1,
    }
}
