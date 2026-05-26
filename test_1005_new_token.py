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

def test_1005_new_token():
    appid = "1005"
    userid = "1977926089"
    token = "78bc44df787f34d58fcb9b95de62f763a3e1bb86ba2184501348cec3079138eb"
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
        "clientver": "12143",
        "clienttime": clienttime,
        "plat": "1",
        "dfid": "-", 
        "mid": "e3fad251748a2a43a92bc05e61844d22", 
        "uuid": "-",
        "userid": userid,
        "token": token
    }
    params["signature"] = sign_params(params, data_str, False)

    url = f"https://gateway.kugou.com/v7/get_all_list"
    
    headers = {
        "Accept": "application/json",
        "Content-Type": "application/json",
        "User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi",
        "x-router": "cloudlist.service.kugou.com"
    }
    
    res = requests.post(url, params=params, data=data_str, headers=headers)
    print(f"1005 new token with actual mid -> {res.status_code}")
    print(res.text[:200])

test_1005_new_token()
