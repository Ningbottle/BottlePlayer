import hashlib
import requests
import time

def test_vip():
    keys = ["appid=3116", "busi_type=concept", "clienttime=" + str(int(time.time())), "clientver=11440", "dfid=2ULHpc3qaLZa43In8x0fLJQp", "mid=d9b28f5ddaf921578f6f6f11d074ca23", "plat=1", "token=bda6cce45bc21ccdbc1748dc62d69a5f7411d26b6e8a9dc7e3afb69f20ed8170", "userid=1977926089", "uuid=-"]
    params_str = "".join(sorted(keys))
    
    salt_lite = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA"
    sig_lite = hashlib.md5((salt_lite + params_str + salt_lite).encode("utf-8")).hexdigest()
    url_lite = "https://kugouvip.kugou.com/v1/get_union_vip?" + "&".join(keys) + "&signature=" + sig_lite
    res_lite = requests.get(url_lite, headers={"User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"})
    print("Lite salt:", res_lite.status_code, res_lite.text[:100])

    salt_std = "OIlwieks28dk2k092lksi2UIkp"
    sig_std = hashlib.md5((salt_std + params_str + salt_std).encode("utf-8")).hexdigest()
    url_std = "https://kugouvip.kugou.com/v1/get_union_vip?" + "&".join(keys) + "&signature=" + sig_std
    res_std = requests.get(url_std, headers={"User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"})
    print("Std salt :", res_std.status_code, res_std.text[:100])

test_vip()
