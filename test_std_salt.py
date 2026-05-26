import hashlib
import requests
import json
import time

def test_std_salt():
    salt = "OIlwieks28dk2k092lksi2UIkp"
    appid = "3116"
    userid = "1977926089"
    token = "bda6cce45bc21ccdbc1748dc62d69a5f7411d26b6e8a9dc7e3afb69f20ed8170"
    clienttime = str(int(time.time()))
    
    data = {
        "userid": int(userid),
        "token": token,
        "total_ver": 979,
        "type": 2,
        "page": 1,
        "pagesize": 30
    }
    data_str = json.dumps(data, separators=(',', ':'))

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
    
    keys = sorted(params.keys())
    param_str = "".join([f"{k}={params[k]}" for k in keys])
    m = hashlib.md5()
    m.update((salt + param_str + data_str + salt).encode('utf-8'))
    params["signature"] = m.hexdigest()

    url = f"https://gateway.kugou.com/v7/get_all_list"
    
    headers = {
        "Accept": "application/json",
        "Content-Type": "application/json",
        "User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi",
        "x-router": "cloudlist.service.kugou.com"
    }
    
    res = requests.post(url, params=params, data=data_str, headers=headers)
    print(f"3116 with std salt -> {res.status_code}")
    print(res.text[:200])

test_std_salt()
