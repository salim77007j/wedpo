// Wedpo browser — fingerprint randomization (JS layer, best-effort like
// mainstream privacy browsers): per-site deterministic canvas/audio noise,
// WebGL vendor spoofing, hardware reporting caps in strict mode.
(function () {
    "use strict";
    var MODE = __WEDPO_MODE__;   // 1 = standard, 2 = strict
    if (location.protocol !== "http:" && location.protocol !== "https:") return;

    function fnv(str) {
        var h = 0x811c9dc5;
        for (var i = 0; i < str.length; i++) {
            h ^= str.charCodeAt(i);
            h = (h + (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24)) >>> 0;
        }
        return h >>> 0;
    }
    var dayKey = Math.floor(Date.now() / 86400000);   // rotates daily
    var seed = fnv(location.hostname) ^ fnv(String(dayKey));

    // deterministic small PRNG
    function rngFactory() {
        var s = seed || 1;
        return function () {
            s ^= s << 13; s >>>= 0;
            s ^= s >> 17;
            s ^= s << 5; s >>>= 0;
            return s / 4294967296;
        };
    }

    // ---- canvas ----
    try {
        var origToDataURL = HTMLCanvasElement.prototype.toDataURL;
        HTMLCanvasElement.prototype.toDataURL = function () {
            try {
                var ctx = this.getContext("2d");
                if (ctx && this.width > 0 && this.height > 0) {
                    var rng = rngFactory();
                    var img = ctx.getImageData(0, 0, Math.min(this.width, 32), Math.min(this.height, 32));
                    for (var i = 0; i < img.data.length; i += 4) {
                        img.data[i] = img.data[i] ^ Math.floor(rng() * 3);
                        img.data[i + 1] = img.data[i + 1] ^ Math.floor(rng() * 3);
                    }
                    ctx.putImageData(img, 0, 0);
                }
            } catch (e) {}
            return origToDataURL.apply(this, arguments);
        };
        var origGetImageData = CanvasRenderingContext2D.prototype.getImageData;
        CanvasRenderingContext2D.prototype.getImageData = function (x, y, w, h) {
            var img = origGetImageData.call(this, x, y, w, h);
            try {
                var rng = rngFactory();
                for (var i = 0; i < img.data.length; i += 4)
                    img.data[i + 3] = img.data[i + 3] ^ (Math.floor(rng() * 2));
            } catch (e) {}
            return img;
        };
    } catch (e) {}

    // ---- audio ----
    try {
        var origGetChannelData = AudioBuffer.prototype.getChannelData;
        AudioBuffer.prototype.getChannelData = function (channel) {
            var data = origGetChannelData.call(this, channel);
            try {
                if (data.length > 0) {
                    var rng = rngFactory();
                    data[0] = data[0] + (rng() - 0.5) * 1e-7;
                }
            } catch (e) {}
            return data;
        };
    } catch (e) {}

    // ---- WebGL ----
    try {
        var spoof = function (proto) {
            var orig = proto.getParameter;
            proto.getParameter = function (param) {
                var v = orig.call(this, param);
                if (typeof v === "string") {
                    if (param === 37445) return "Mozilla";                    // UNMASKED_VENDOR_WEBGL
                    if (param === 37446) return "Wedpo Generic Renderer";     // UNMASKED_RENDERER_WEBGL
                }
                return v;
            };
        };
        if (window.WebGLRenderingContext) spoof(WebGLRenderingContext.prototype);
        if (window.WebGL2RenderingContext) spoof(WebGL2RenderingContext.prototype);
    } catch (e) {}

    // ---- strict hardware caps ----
    if (MODE >= 2) {
        try { Object.defineProperty(navigator, "hardwareConcurrency", { get: function () { return 4; } }); } catch (e) {}
        try { Object.defineProperty(navigator, "deviceMemory", { get: function () { return 8; } }); } catch (e) {}
        try { Object.defineProperty(navigator, "userAgentData", { get: function () { return undefined; } }); } catch (e) {}
    }
})();
