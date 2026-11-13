import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

ApplicationWindow {
    id: root
    width: 1150
    height: 760
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: "BearPaw 2448 CU Pro — bearpaw-scan"

    property bool deviceReady: false
    property bool scanning: false
    property string imagePath: ""
    property string imageInfo: ""

    color: "#1e1f24"

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // ------- верхняя панель -------
        RowLayout {
            Layout.margins: 10
            Layout.fillWidth: true
            spacing: 10

            Button {
                text: "Поиск устройства"
                enabled: !root.scanning
                onClicked: worker.findDevice()
            }
            Button {
                text: root.scanning ? "Сканирование…" : "Сканировать"
                enabled: root.deviceReady && !root.scanning
                onClicked: {
                    root.scanning = true
                    worker.startScan()
                }
            }
            Rectangle {
                width: 14; height: 14; radius: 7
                color: root.scanning ? "#e6b450" : (root.deviceReady ? "#61c46a" : "#c45a5a")
            }
            Label {
                text: root.deviceReady
                      ? (root.scanning ? stageText.text : "устройство готово")
                      : "устройство не найдено"
                color: "#c9cbd1"
            }
            Item { Layout.fillWidth: true }
            Label {
                text: "лог: " + logModel.logFilePath()
                color: "#7a7d87"
                font.pixelSize: 11
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally("file://" + logModel.logFilePath())
                }
            }
        }

        // собственный прогресс-бар: не зависит от системного QML-стиля
        // (Breeze ProgressBar у некоторых пользователей падает)
        Rectangle {
            id: progressBg
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.topMargin: 0
            Layout.preferredHeight: 14
            radius: 4
            color: "#2a2c33"
            border.color: "#3a3d46"; border.width: 1
            property real value: 0
            Rectangle {
                id: progressFill
                height: parent.height - 2
                radius: 3
                color: root.scanning ? "#e6b450" : "#61c46a"
                width: Math.max(0, Math.min(1, parent.value / 100)) * (parent.width - 2) + 2
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Label {
            id: stageText
            Layout.margins: 10
            Layout.topMargin: 0
            text: ""
            color: "#9aa0ac"
            font.pixelSize: 12
        }

        // ------- центр: картинка -------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            color: "#14151a"
            radius: 6
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: 6
                source: root.imagePath
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
            }
            Label {
                anchors.centerIn: parent
                visible: root.imagePath === ""
                text: "здесь появится скан"
                color: "#565963"
            }
            Label {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.margins: 8
                visible: root.imageInfo !== ""
                text: root.imageInfo
                color: "#8b8e97"
                font.pixelSize: 11
            }
        }

        // ------- низ: лог -------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 210
            Layout.margins: 10
            color: "#14151a"
            radius: 6
            clip: true

            ListView {
                id: logView
                anchors.fill: parent
                anchors.margins: 6
                model: logModel
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Text {
                    width: logView.width
                    text: model.text
                    color: text.indexOf("!!") === 0 || text.indexOf("ошибка") >= 0 ? "#e07a6a" : "#b9bcc4"
                    font.family: "monospace"
                    font.pixelSize: 12
                    wrapMode: Text.WrapAnywhere
                }
                onCountChanged: Qt.callLater(positionViewAtEnd)
            }
        }
    }

    Component.onCompleted: {
        if (autotest) {
            worker.findDevice()
            worker.startScan()
        }
    }

    Connections {
        target: worker
        function onDeviceFound(path, desc) { root.deviceReady = true }
        function onDeviceLost() { root.deviceReady = false }
        function onProgress(pct, stage) { progressBg.value = pct; stageText.text = stage }
        function onScanFinished(path, w, h, bytes) {
            root.scanning = false
            root.imagePath = path
            root.imageInfo = "%1×%2, %3 МБ".arg(w).arg(h).arg((bytes / 1048576).toFixed(1))
        }
        function onScanFailed(reason) {
            root.scanning = false
            stageText.text = "ошибка: " + reason
            logModel.append("!! " + reason)
        }
    }
}
