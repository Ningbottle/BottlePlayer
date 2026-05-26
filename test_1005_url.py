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

def test_1005_url_new_token():
    appid = "1005"
    userid = "1977926089"
    token = "78bc44df787f34d58fcb9b95de62f763a3e1bb86ba2184501348cec3079138eb"
    clienttime = str(int(time.time()))
    
    # Random song hash for testing
    song_hash = "67EDCEF4CD1F5E12E9903DE9D603F252"
    
    params = {
        "appid": appid,
        "clientver": "12143",
        "clienttime": clienttime,
        "plat": "1",
        "dfid": "-", 
        "mid": "e3fad251748a2a43a92bc05e61844d22", 
        "uuid": "-",
        "userid": userid,
        "token": token,
        "album_id": "0",
        "album_audio_id": "0",
        "hash": song_hash
    }
    params["signature"] = sign_params(params, "", False)

    url = f"https://gateway.kugou.com/v5/url"
    
    headers = {
        "Accept": "application/json",
        "User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi",
        "x-router": "tracker.kugou.com"
    }
    
    res = requests.get(url, params=params, headers=headers)
    print(f"1005 url endpoint -> {res.status_code}")
    print(res.text[:200])

test_1005_url_new_token()
