use wedpo_core::WedpoEngine;

const TEST_LIST: &str = "\
! test filter list
||doubleclick.net^
||ads.example.com^
/banner/ad.js$script
##.ad-banner
##[id=\"sponsored-box\"]
#@#.ok-banner
";

#[test]
fn blocks_network_requests() {
    let e = WedpoEngine::new();
    e.load_list("test", TEST_LIST);
    let mut redirect = String::new();
    assert_eq!(e.check("https://ad.doubleclick.net/ddm/adj/x", "image", "news.example.com", &mut redirect), 1);
    assert_eq!(e.check("https://ads.example.com/pixel.gif", "image", "news.example.com", &mut redirect), 1);
    assert_eq!(e.check("https://cdn.example.com/banner/ad.js", "script", "news.example.com", &mut redirect), 1);
    assert_eq!(e.check("https://example.com/photo.jpg", "image", "example.com", &mut redirect), 0);
    assert_eq!(e.check("https://plain.example.org/page", "main_frame", "", &mut redirect), 0);
}

#[test]
fn allows_without_lists() {
    let e = WedpoEngine::new();
    let mut redirect = String::new();
    assert_eq!(e.check("https://ad.doubleclick.net/x", "image", "x.com", &mut redirect), 0);
}

#[test]
fn cosmetic_css_generated() {
    let e = WedpoEngine::new();
    e.load_list("test", TEST_LIST);
    let _ = e.cosmetic_css("https://news.example.com/article/1");
    // Generic class/id rules are resolved through the two-phase DOM API.
    let css = e.generic_css(&["ad-banner".to_string()], &[], &[]);
    assert!(css.contains(".ad-banner"), "css: {css}");
    assert!(css.contains("display:none!important"));
    // Exception respected
    let css_ok = e.generic_css(&["ok-banner".to_string()], &[], &[]);
    assert!(!css_ok.contains(".ok-banner"), "css: {css_ok}");
}

#[test]
fn strips_tracking_parameters() {
    let e = WedpoEngine::new();
    let url = "https://example.com/page?id=7&utm_source=facebook&utm_medium=cpc&fbclid=IwAR123&keep=1";
    let stripped = e.strip_tracking(url).unwrap();
    assert!(stripped.starts_with("https://example.com/page?"));
    assert!(!stripped.contains("utm_source"));
    assert!(!stripped.contains("fbclid"));
    assert!(stripped.contains("id=7"));
    assert!(stripped.contains("keep=1"));
    assert!(e.strip_tracking("https://example.com/clean?a=1").is_none());
    assert!(e.strip_tracking("file:///tmp/x?utm_source=z").is_none());
}

#[test]
fn download_risk_scoring() {
    let e = WedpoEngine::new();
    let (score, reasons) = e.download_risk("http://bit.ly/setup.pdf.exe", "application/octet-stream");
    assert!(score >= 80, "score={score} reasons={reasons:?}");
    assert!(reasons.iter().any(|r| r.contains("Executable")));
    assert!(reasons.iter().any(|r| r.contains("Disguised")));
    assert!(reasons.iter().any(|r| r.contains("shortener")));

    let (safe, _) = e.download_risk("https://example.com/report.pdf", "application/pdf");
    assert_eq!(safe, 0);

    let (mid, _) = e.download_risk("https://example.com/backup.zip", "application/zip");
    assert!(mid >= 10 && mid <= 30, "mid={mid}");
}

#[test]
fn lists_info_json() {
    let e = WedpoEngine::new();
    e.load_list("EasyList TEST", TEST_LIST);
    let info = e.lists_info();
    assert!(info.contains("\"name\":\"EasyList TEST\""), "info={info}");
    assert!(info.contains("\"count\":1"));
    e.remove_list("EasyList TEST");
    assert!(e.lists_info().contains("\"count\":0"));
}

#[test]
fn thread_safety_smoke() {
    let e = std::sync::Arc::new(WedpoEngine::new());
    e.load_list("test", TEST_LIST);
    let mut handles = Vec::new();
    for _ in 0..4 {
        let e = e.clone();
        handles.push(std::thread::spawn(move || {
            let mut r = String::new();
            for _ in 0..100 {
                assert_eq!(e.check("https://ad.doubleclick.net/x", "image", "x.com", &mut r), 1);
                let _ = e.cosmetic_css("https://news.example.com/a");
            }
        }));
    }
    for h in handles {
        h.join().unwrap();
    }
}
