import ctypes, sys

dll = ctypes.CDLL(r"C:\Users\salim\.zcode\workspace\default\wedpo\core\target\release\wedpo_core.dll")

dll.wedpo_core_version.restype = ctypes.c_char_p
dll.wedpo_engine_new.restype = ctypes.c_void_p
dll.wedpo_engine_free.argtypes = [ctypes.c_void_p]
dll.wedpo_engine_load_list.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
dll.wedpo_engine_remove_list.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
dll.wedpo_engine_check.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
dll.wedpo_engine_cosmetic.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
dll.wedpo_engine_generic_css.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
dll.wedpo_engine_strip_tracking.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
dll.wedpo_engine_risk.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
dll.wedpo_engine_lists_info.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]

print("version:", dll.wedpo_core_version().decode())

e = dll.wedpo_engine_new()
assert e, "engine_new returned NULL"

TEST_LIST = b"""! test list
||doubleclick.net^
||ads.example.com^
/banner/ad.js$script
##.ad-banner
##[id="sponsored-box"]
#@#.ok-banner
"""
assert dll.wedpo_engine_load_list(e, b"test", TEST_LIST) == 1

redirect = ctypes.create_string_buffer(2048)
r = dll.wedpo_engine_check(e, b"https://ad.doubleclick.net/ddm/adj/x", b"image", b"news.example.com", redirect, 2048)
print("block doubleclick:", r); assert r == 1
r = dll.wedpo_engine_check(e, b"https://example.com/photo.jpg", b"image", b"example.com", redirect, 2048)
print("allow normal:", r); assert r == 0

css = ctypes.create_string_buffer(65536)
n = dll.wedpo_engine_generic_css(e, b"ad-banner", b"", b"", css, 65536)
print("generic css n=%d: %s" % (n, css.value[:120]))
assert b".ad-banner" in css.value and n > 0

out = ctypes.create_string_buffer(2048)
r = dll.wedpo_engine_strip_tracking(e, b"https://example.com/p?a=1&utm_source=facebook&fbclid=XYZ&keep=1", out, 2048)
clean = out.value.decode()
print("strip:", r, clean)
assert r == 1 and "utm_source" not in clean and "fbclid" not in clean and "a=1" in clean and "keep=1" in clean

reasons = ctypes.create_string_buffer(4096)
score = dll.wedpo_engine_risk(e, b"http://bit.ly/x.exe", b"application/octet-stream", reasons, 4096)
print("risk:", score, reasons.value.decode())
assert score == 75, score  # http(+10) + shortener(+20) + exe(+45), deterministic

info = ctypes.create_string_buffer(4096)
dll.wedpo_engine_lists_info(e, info, 4096)
print("lists:", info.value.decode())
assert b'"count":1' in info.value

assert dll.wedpo_engine_remove_list(e, b"test") == 1
dll.wedpo_engine_free(e)
print("ALL C-ABI SMOKE TESTS PASSED")
