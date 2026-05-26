import hashlib
import requests
import json
import time

def sign_params(params, data_str, is_lite=False):
    salt = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA" if is_lite else "OIlwieks28dk2k092lksi2UIkp"
    keys = sorted(params.keys())
    param_str = "".join([f"{k}={params[k]}" for k in keys])
    m = hashlib.md5()
    m.update((salt + param_str + data_str + salt).encode('utf-8'))
    return m.hexdigest()

def test_song_url(appid, clientver, is_lite_sig):
    userid = "1977926089"
    token = "bda6cce45bc21ccdbc1748dc62d69a5f7411d26b6e8a9dc7e3afb69f20ed8170"
    clienttime = str(int(time.time()))
    hash_val = "eb45c61d5635b43cc0965e640e0c46b9" # random lowercase hash
    
    params = {
        "album_id": "0",
        "area_code": "1",
        "hash": hash_val,
        "ssa_flag": "is_fromtrack",
        "version": "11430",
        "page_id": "151369488",
        "quality": "128",
        "album_audio_id": "0",
        "behavior": "play",
        "pid": "2",
        "cmd": "26",
        "pidversion": "3001",
        "IsFreePart": "0",
        "ppage_id": "463467626,350369493,788954147",
        "cdnBackup": "1",
        "module": "",
        "appid": appid,
        "clientver": clientver,
        "mid": "0",
        "dfid": "-",
        "uuid": "-",
        "userid": userid,
        "token": token
    }
    
    # Calculate v5 key
    isLiteKey = (appid == "1014")
    str_val = "185672dd44712f60bb1736df5a377e82" if isLiteKey else "57ae12eb6890223e355ccfcb74edf70d"
    m_key = hashlib.md5()
    m_key.update((hash_val + str_val + appid + "0" + userid).encode('utf-8'))
    params["key"] = m_key.hexdigest()

    params["clienttime"] = clienttime
    params["signature"] = sign_params(params, "", is_lite_sig)

    url = f"https://gateway.kugou.com/v5/url"
    
    headers = {
        "Accept": "application/json",
        "User-Agent": "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi",
        "x-router": "trackercdn.kugou.com",
        "dfid": "-",
        "clienttime": clienttime,
        "mid": "0",
        "kg-rc": "1",
        "kg-thash": "5d816a0",
        "kg-rec": "1",
        "kg-rf": "B9EDA08A64250DEFFBCADDEE00F8"
    }
    
    res = requests.get(url, params=params, headers=headers)
    print(f"song_url appid={appid} is_lite_sig={is_lite_sig} -> {res.status_code}")
    print(res.text[:200])

test_song_url("1005", "12143", False)
test_song_url("3116", "11440", True)
test_song_url("3116", "11440", False)
