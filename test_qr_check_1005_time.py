import hashlib
import requests
import json
import time

def sign_web_params(params):
    salt = "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt"
    keys = sorted(params.keys())
    param_str = "".join([f"{k}={params[k]}" for k in keys])
    # calculate md5
    m = hashlib.md5()
    m.update((salt + param_str + salt).encode('utf-8'))
    return m.hexdigest()

def test_qr_check():
    appid = "1005"
    
    params = {
        "appid": appid,
        "clientver": "20489",
        "plat": "4",
        "qrcode": "b5db0e9ea98db2da3c62372ab54b9d5c5896a244", # Dummy qrcode
        "srcappid": "2919",
        "clienttime": str(int(time.time())),
        "mid": "e3fad251748a2a43a92bc05e61844d22",
        "uuid": "-",
        "dfid": "-"
    }
    params["signature"] = sign_web_params(params)

    url = f"https://login-user.kugou.com/v2/get_userinfo_qrcode"
    
    res = requests.get(url, params=params)
    print(f"QR Check with {appid} -> {res.status_code}")
    print(res.text[:200])

test_qr_check()
