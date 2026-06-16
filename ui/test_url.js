import crypto from 'crypto';

const cryptoMd5 = (str) => crypto.createHash('md5').update(str).digest('hex');

const signKey = (hash, mid, userid, appid) => {
    const isLite = appid === '1014';
    const str = isLite ? '185672dd44712f60bb1736df5a377e82' : '57ae12eb6890223e355ccfcb74edf70d';
    return cryptoMd5(`${hash}${str}${appid}${mid}${userid || 0}`);
};

const signatureAndroidParams = (params) => {
    const salt = "OIlwieks28dk2k092lksi2UIkp";
    const keys = Object.keys(params).sort();
    let paramsString = '';
    for (const key of keys) {
        paramsString += key + "=" + params[key];
    }
    return cryptoMd5(salt + paramsString + salt);
};

const hash = '90B8469459CBA58A5DDEDD9350286DD8'; // 十面埋伏 VIP song
const appid = '1005'; 
const mid = 'your_mid_here';
const dfid = 'your_dfid_here';
const clientver = '11430';
const userid = '0'; // testing without login first
const token = 'your_token_here';

const params = {
    album_id: 0,
    area_code: 1,
    hash: hash.toLowerCase(),
    ssa_flag: 'is_fromtrack',
    version: clientver,
    page_id: 151369488,
    quality: 128,
    album_audio_id: 0,
    behavior: 'play',
    pid: 2,
    cmd: 26,
    pidversion: 3001,
    IsFreePart: 0,
    ppage_id: '463467626,350369493,788954147',
    cdnBackup: 1,
    module: '',
    clientver: clientver,
    appid,
    mid,
    dfid,
    clienttime: Math.floor(Date.now() / 1000),
    uuid: '0',
    userid,
    token
};

params.key = signKey(params.hash, params.mid, params.userid, params.appid);
params.signature = signatureAndroidParams(params);

const searchParams = new URLSearchParams(params);

fetch(`https://gateway.kugou.com/v5/url?${searchParams.toString()}`, {
    headers: {
        'x-router': 'trackercdn.kugou.com',
        'User-Agent': 'Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi'
    }
}).then(res => res.json()).then(res => {
    console.log(JSON.stringify(res, null, 2));
}).catch(err => console.error(err));
