import hashlib
import requests
import json
import time

def sign_params(params, data_str, is_lite=False):
    salt = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA" if is_lite else "OIlwieks28dk2k092lksi2UIkp"
    keys = sorted(params.keys())
    param_str = "".join([f"{k}={params[k]}" for k in keys])
    # calculate md5
    m = hashlib.md5()
    m.update((salt + param_str + data_str + salt).encode('utf-8'))
    return m.hexdigest()

def test_cloud_list():
    appid = "3116"
    userid = "1977926089"
    token = "bda6cce45bc21ccdbc1748dc62d69a5f7411d26b6e8a9dc7e3afb69f20ed8170"
    clienttime = str(int(time.time()))
    
    params = {
        "appid": appid,
        "clientver": "11440",
        "clienttime": clienttime,
        "plat": "1",
        "dfid": "2ULHpc3qaLZa43In8x0fLJQp",
        "mid": "d9b28f5ddaf921578f6f6f11d074ca23",
        "uuid": "-",
        "userid": userid,
        "token": token
    }
    params["signature"] = sign_params(params, "", True)

    url = f"https://gateway.kugou.com/v1/cloud/get_cloud_list"
    
    headers = {
        "Accept": "application/json",
        "Content-Type": "application/json",
        "User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi",
        "x-router": "cloudlist.service.kugou.com"
    }
    
    res = requests.post(url, params=params, data="", headers=headers)
    print(f"cloud_list -> {res.status_code}")
    print(res.text[:200])

test_cloud_list()
