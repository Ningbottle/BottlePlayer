import hashlib
import requests
import json
import time
import sys

def sign_params(params, data_str, is_lite=False):
    salt = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA" if is_lite else "OIlwieks28dk2k092lksi2UIkp"
    keys = sorted(params.keys())
    param_str = "".join([f"{k}={params[k]}" for k in keys])
    # calculate md5
    m = hashlib.md5()
    m.update((salt + param_str + data_str + salt).encode('utf-8'))
    return m.hexdigest()

def test_qr_create():
    appid = "1005"
    clientver = "12143"
    
    params = {
        "appid": appid,
        "clientver": clientver,
        "type": "1",
        "plat": "4",
        "qrcode_txt": f"https://h5.kugou.com/apps/loginQRCode/html/index.html?appid={appid}&",
        "srcappid": "2919",
        "clienttime": str(int(time.time())),
        "mid": "e3fad251748a2a43a92bc05e61844d22",
        "dfid": "-"
    }
    params["signature"] = sign_params(params, "", False)

    url = f"https://login-user.kugou.com/v2/qrcode"
    
    res = requests.get(url, params=params)
    print(f"QR Create with {appid} -> {res.status_code}")
    print(res.text[:200])

test_qr_create()
