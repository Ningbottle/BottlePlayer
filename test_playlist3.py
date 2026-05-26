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

def test_playlist(appid, is_lite, endpoint, use_get=False):
    userid = "1977926089"
    token = "bda6cce45bc21ccdbc1748dc62d69a5f7411d26b6e8a9dc7e3afb69f20ed8170"
    clienttime = str(int(time.time()))
    
    if not use_get:
        data = {
            "userid": userid, # try string
            "token": token,
            "total_ver": 979,
            "type": 2,
            "page": 1,
            "pagesize": 30
        }
        data_str = json.dumps(data, separators=(',', ':'))
    else:
        data_str = ""
        
    params = {
        "appid": appid,
        "clientver": "11440" if is_lite else "12143",
        "clienttime": clienttime,
        "plat": "1",
        "dfid": "2ULHpc3qaLZa43In8x0fLJQp",
        "mid": "d9b28f5ddaf921578f6f6f11d074ca23",
        "uuid": "-",
        "userid": userid,
        "token": token
    }
    params["signature"] = sign_params(params, data_str, is_lite)

    url = f"https://gateway.kugou.com/{endpoint}"
    
    headers = {
        "Accept": "application/json",
        "User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi",
    }
    
    if not use_get:
        headers["Content-Type"] = "application/json"
        headers["x-router"] = "cloudlist.service.kugou.com"
        res = requests.post(url, params=params, data=data_str, headers=headers)
    else:
        res = requests.get(url, params=params, headers=headers)
        
    print(f"{endpoint} -> {res.status_code}")
    print(res.text[:200])

print("Test 3116 v7/get_all_list string")
test_playlist("3116", True, "v7/get_all_list")
print("Test 3116 v2/get_list_file")
test_playlist("3116", True, "v2/get_list_file")
print("Test 3116 v5/get_all_list")
test_playlist("3116", True, "v5/get_all_list")

