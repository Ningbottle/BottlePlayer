import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BottleMusic

Column {
    id: root
    objectName: "accountPanel"
    spacing: Theme.s5
    Component.onCompleted: accountState.activate()
    Component.onDestruction: accountState.deactivate()
    RowLayout {
        width: parent.width; spacing: Theme.s4; visible: accountState.loggedIn
        Cover { width: 56; height: width; radius: width / 2; url: accountState.avatar; title: accountState.username; pixels: 200 }
        Column {
            Layout.fillWidth: true; spacing: Theme.s1
            Text { width: parent.width; text: accountState.username; textFormat: Text.PlainText; elide: Text.ElideRight; color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: Theme.fsXl }
            Text { text: "酷狗账号，" + accountState.userId; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs }
        }
    }
    Column {
        width: parent.width; spacing: Theme.s3; visible: !accountState.loggedIn
        Text { text: "扫码登录"; color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: Theme.fsXl }
        Text { width: parent.width; wrapMode: Text.Wrap; text: "使用酷狗音乐 App 扫码登录"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsMd }
        Rectangle {
            visible: accountState.qrImage.length > 0
            width: Math.min(root.width, 216); height: width; color: Theme.white; radius: Theme.rSm
            Image { objectName: "loginQrImage"; anchors.fill: parent; source: accountState.qrImage; smooth: false; cache: false; fillMode: Image.PreserveAspectFit }
        }
    }
    Text {
        objectName: "accountStatus"; width: parent.width; wrapMode: Text.Wrap
        text: accountState.ready ? accountState.message : "正在连接音乐核心…"
        color: accountState.state === "error" ? Theme.danger : Theme.text2
        font.family: Theme.fontUi; font.pixelSize: Theme.fsSm
        Accessible.role: Accessible.StaticText
    }
    Row {
        spacing: Theme.s2
        ActionButton {
            objectName: "loginRefresh"; visible: !accountState.loggedIn
            text: accountState.state === "requesting" ? "正在生成…" : accountState.qrImage.length ? "刷新二维码" : "生成二维码"
            glyph: "refresh-cw"; filled: true; enabled: accountState.ready && !accountState.busy
            onClicked: accountState.beginLogin()
        }
        ActionButton {
            objectName: "loginCancel"; visible: !accountState.loggedIn && ["requesting", "waiting", "scanned"].includes(accountState.state)
            text: "取消"; onClicked: accountState.cancelLogin()
        }
        ActionButton { objectName: "accountRefresh"; visible: accountState.loggedIn; text: "刷新账户"; glyph: "refresh-cw"; enabled: !accountState.busy; onClicked: accountState.refreshSession() }
        ActionButton { objectName: "accountLogout"; visible: accountState.loggedIn; text: "退出登录"; quiet: true; enabled: !accountState.busy; onClicked: accountState.logout() }
    }
    Column {
        visible: accountState.loggedIn; width: parent.width; spacing: Theme.s3
        Hairline { width: parent.width; color: Theme.border }
        CssText { text: vipState.label; color: Theme.text; font.family: Theme.fontTitle; font.pixelSize: Theme.fsXl }
        CssText { visible: vipState.end.length > 0; text: "有效期至 " + vipState.end; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsSm }
        Text { visible: vipState.caution.length > 0; width: parent.width; text: vipState.caution; wrapMode: Text.Wrap; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
        Row {
            spacing: Theme.s2
            ActionButton { objectName: "claimDailyVip"; text: vipState.busy ? "领取中…" : "领取每日 VIP"; filled: true; enabled: !vipState.busy && !vipState.loading; onClicked: vipState.claim() }
            ActionButton { objectName: "cancelVipClaim"; visible: vipState.busy; text: "停止"; onClicked: vipState.cancel() }
            ActionButton { objectName: "refreshVip"; text: vipState.loading ? "查询中…" : "刷新权益"; enabled: !vipState.busy && !vipState.loading; onClicked: vipState.refresh() }
        }
        Text { visible: vipState.message.length > 0; width: parent.width; text: vipState.message; wrapMode: Text.Wrap; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm }
        Text { visible: vipState.accepted.length > 0; width: parent.width; text: vipState.accepted; wrapMode: Text.Wrap; color: Theme.text3; font.family: Theme.fontMeta; font.pixelSize: Theme.fsXs }
    }
    Hairline { width: parent.width; color: Theme.border }
    Row {
        visible: accountState.loggedIn; spacing: Theme.s2
        ActionButton { objectName: "syncLocalFavorites"; text: "同步本地收藏"; enabled: !favoriteState.loading; onClicked: favoriteState.syncLocal() }
        ActionButton { objectName: "retryFavoriteSync"; text: "重试收藏同步"; enabled: !favoriteState.loading; onClicked: favoriteState.retry() }
    }
    Text { visible: favoriteState.pendingCount > 0; text: "等待同步的收藏操作：" + favoriteState.pendingCount; font.family: Theme.fontUi; font.pixelSize: Theme.fsSm; color: Theme.warning }
}
