import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    readonly property real cameraSpin: spin.value
    readonly property real cameraTilt: tilt.value
    readonly property real cameraDistance: distance.value
    property real cameraPanX: 0
    property real cameraPanY: 0
    property real cameraPanZ: 0
    property real coordinateX: 0
    property real coordinateY: 0
    property real coordinateZ: 0
    property real bodyHeight: 1.78
    onBodyHeightChanged: bodyChanged()
    property real genderBlend: 1
    onGenderBlendChanged: bodyChanged()
    property real chest: 1
    onChestChanged: bodyChanged()
    property real waist: 1
    onWaistChanged: bodyChanged()
    property real hips: 1
    onHipsChanged: bodyChanged()
    property real weight: 1
    onWeightChanged: bodyChanged()
    property real armLength: 1
    onArmLengthChanged: bodyChanged()
    property real legLength: 1
    onLegLengthChanged: bodyChanged()
    property real headScale: 1
    onHeadScaleChanged: bodyChanged()
    property bool shirtEnabled: true
    onShirtEnabledChanged: shirtChanged()
    signal shirtChanged
    property bool clothEnabled: true
    property bool clothRunning: true
    property real clothWind: 0.8
    onClothEnabledChanged: clothSettingsChanged()
    onClothRunningChanged: clothSettingsChanged()
    onClothWindChanged: clothSettingsChanged()
    signal clothSettingsChanged
    signal resetCloth
    property bool humanPage: true
    signal openObj
    signal showCube
    signal bodyChanged

    signal settingsChanged

    function setCamera(spinValue, tiltValue, distanceValue) {
        spin.value = spinValue;
        tilt.value = tiltValue;
        distance.value = distanceValue;
    }

    onCameraSpinChanged: settingsChanged()
    onCameraTiltChanged: settingsChanged()
    onCameraDistanceChanged: settingsChanged()
    onCameraPanXChanged: settingsChanged()
    onCameraPanYChanged: settingsChanged()
    onCameraPanZChanged: settingsChanged()
    onCoordinateXChanged: settingsChanged()
    onCoordinateYChanged: settingsChanged()
    onCoordinateZChanged: settingsChanged()

    width: 280
    height: 800
    color: "#1c293c"

    ScrollView {
      anchors.fill: parent
      clip: true
      ColumnLayout {
        width: panel.width - 30
        spacing: 6

        Label { text: "Human model"; color: "white"; font.pixelSize: 22 }
        RowLayout {
            Button { text: "Open OBJ"; onClicked: panel.openObj() }
            Button { text: "Cube"; onClicked: panel.showCube() }
        }
        Button { text: "Create human"; onClicked: panel.bodyChanged() }
        ColumnLayout {
          visible: panel.humanPage
          Layout.fillWidth: true
        Label { text: "MakeHuman body parameters"; color: "#b8c9db" }
        Label { text: "Height (m): " + panel.bodyHeight.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 1.2; to: 2.2; value: panel.bodyHeight; onMoved: panel.bodyHeight = value }
        Label { text: "Gender: " + panel.genderBlend.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 0; to: 1; value: panel.genderBlend; onMoved: panel.genderBlend = value }
        Label { text: "Chest: " + panel.chest.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 0.5; to: 1.5; value: panel.chest; onMoved: panel.chest = value }
        Label { text: "Waist: " + panel.waist.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 0.5; to: 1.5; value: panel.waist; onMoved: panel.waist = value }
        Label { text: "Hips: " + panel.hips.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 0.5; to: 1.5; value: panel.hips; onMoved: panel.hips = value }
        Label { text: "Weight: " + panel.weight.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 0.5; to: 1.5; value: panel.weight; onMoved: panel.weight = value }
        Label { text: "Arm length: " + panel.armLength.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 0.5; to: 1.5; value: panel.armLength; onMoved: panel.armLength = value }
        Label { text: "Leg length: " + panel.legLength.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 0.5; to: 1.5; value: panel.legLength; onMoved: panel.legLength = value }
        Label { text: "Head scale: " + panel.headScale.toFixed(2); color: "white" }
        Slider { Layout.fillWidth: true; from: 0.5; to: 1.5; value: panel.headScale; onMoved: panel.headScale = value }
        }
        Label { text: "Cloth simulation"; color: "white"; font.pixelSize: 20 }
        CheckBox { text: "Wear shirt"; checked: panel.shirtEnabled; onToggled: panel.shirtEnabled = checked }
        CheckBox { text: "Wear simulated skirt"; checked: panel.clothEnabled; onToggled: panel.clothEnabled = checked }
        RowLayout {
            Button { text: panel.clothRunning ? "Pause" : "Resume"; enabled: panel.clothEnabled; onClicked: panel.clothRunning = !panel.clothRunning }
            Button { text: "Reset cloth"; enabled: panel.clothEnabled; onClicked: panel.resetCloth() }
        }
        Label { text: "Wind: " + panel.clothWind.toFixed(1); color: "white" }
        Slider { Layout.fillWidth: true; from: 0; to: 4; value: panel.clothWind; onMoved: panel.clothWind = value }
        Label { text: "Waist-pinned skirt · static body"; color: "#b8c9db" }
        Label { text: "View parameters"; color: "white"; font.pixelSize: 20 }
        Label {
            text: "Rotation"
            color: "white"
        }

        Slider {
            id: spin

            Layout.fillWidth: true
            from: -180
            to: 180
            value: 0
        }

        Label {
            text: "Tilt"
            color: "white"
        }

        Slider {
            id: tilt

            Layout.fillWidth: true
            from: -90
            to: 90
            value: 0
        }

        Label {
            text: "Camera distance"
            color: "white"
        }

        Slider {
            id: distance

            Layout.fillWidth: true
            from: 20
            to: 5000
            value: 2600
        }

        Button {
            text: "Reset view"
            onClicked: {
                spin.value = 0;
                tilt.value = 0;
                distance.value = 2600;
                panel.cameraPanX = 0;
                panel.cameraPanY = 0;
                panel.cameraPanZ = 0;
                panel.coordinateX = 0;
                panel.coordinateY = 0;
                panel.coordinateZ = 0;
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
    }
}
