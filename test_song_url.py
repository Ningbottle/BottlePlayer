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

def test_song_url():
    appid = "1005"
    userid = "1977926089"
    token = "bda6cce45bc21ccdbc1748dc62d69a5f7411d26b6e8a9dc7e3afb69f20ed8170"
    clienttime = str(int(time.time()))
    
    params = {
        "appid": appid,
        "clientver": "12143",
        "clienttime": clienttime,
        "dfid": "-",
        "mid": "0",
        "userid": userid,
        "token": token,
        "platid": "4",
        "album_audio_id": "848243407" # Some random song
    }
    params["signature"] = sign_params(params, "", False)

    url = f"https://gateway.kugou.com/v5/url/get"
    
    headers = {
        "Accept": "application/json",
        "Content-Type": "application/json",
        "User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi",
        "x-router": "media.store.kugou.com"
    }
    
    res = requests.get(url, params=params, headers=headers)
    print(f"song_url -> {res.status_code}")
    print(res.text[:200])

test_song_url()
