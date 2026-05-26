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

def test_qr_create_web_mixed():
    appid = "1014"
    
    params = {
        "appid": appid,
        "type": "1",
        "plat": "4",
        "qrcode_txt": f"https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=1005&",
        "srcappid": "2919"
    }
    params["signature"] = sign_web_params(params)

    url = f"https://login-user.kugou.com/v2/qrcode"
    
    res = requests.get(url, params=params)
    print(f"QR Create with {appid} (web) mixed -> {res.status_code}")
    print(res.text[:200])

test_qr_create_web_mixed()
