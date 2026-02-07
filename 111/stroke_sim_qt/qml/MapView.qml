import QtQuick 2.15
import QtLocation 6.5
import QtPositioning 6.5

Item {
  id: root
  property var pathPoints: []
  property bool hasCenter: false
  property real defaultLat: 31.9778222
  property real defaultLon: 120.9097313

  ListModel {
    id: strokeModel
  }

  function addPoint(lat, lon) {
    if (!isFinite(lat) || !isFinite(lon)) return;
    var coord = QtPositioning.coordinate(lat, lon);
    pathPoints.push(coord);
    track.path = pathPoints;
    marker.coordinate = coord;
    marker.visible = true;
    // 若未设置中心，首次定位
    if (!hasCenter) {
      map.center = coord;
      map.zoomLevel = 16;
      hasCenter = true;
    }
  }

  function addStrokeMarker(lat, lon) {
    if (!isFinite(lat) || !isFinite(lon)) return;
    // 取消上一桨高亮
    if (strokeModel.count > 0) {
      strokeModel.setProperty(strokeModel.count - 1, "current", false);
    }
    strokeModel.append({lat: lat, lon: lon, current: true});
  }

  function resetTrack() {
    pathPoints = [];
    track.path = pathPoints;
    marker.visible = false;
    hasCenter = false;
    map.center = QtPositioning.coordinate(defaultLat, defaultLon);
    map.zoomLevel = 16;
    strokeModel.clear();
  }

  function setInitialCenter(lat, lon) {
    if (!isFinite(lat) || !isFinite(lon)) return;
    var coord = QtPositioning.coordinate(lat, lon);
    defaultLat = lat;
    defaultLon = lon;
    map.center = coord;
    map.zoomLevel = 16;
    hasCenter = true;
  }

  Map {
    id: map
    anchors.fill: parent
    plugin: Plugin { name: "osm" }
    center: QtPositioning.coordinate(defaultLat, defaultLon)
    zoomLevel: 16
    minimumZoomLevel: 2
    maximumZoomLevel: 22

    MapPolyline {
      id: track
      line.width: 3
      line.color: "#1E88E5"
      path: []
    }

    MapItemView {
      model: strokeModel
      delegate: MapQuickItem {
        coordinate: QtPositioning.coordinate(lat, lon)
        anchorPoint.x: width/2
        anchorPoint.y: height/2
        sourceItem: Rectangle {
          width: current ? 12 : 8
          height: width
          radius: width/2
          color: current ? "#E53935" : "#FDD835"
          border.color: "white"
          border.width: 1
        }
      }
    }

    MapQuickItem {
      id: marker
      visible: false
      anchorPoint.x: 4
      anchorPoint.y: 4
      sourceItem: Rectangle {
        width: 8
        height: 8
        radius: 4
        color: "#E53935"
        border.color: "white"
        border.width: 1
      }
    }
  }

  MouseArea {
    id: panArea
    anchors.fill: parent
    hoverEnabled: true
    acceptedButtons: Qt.LeftButton
    property real lastX: 0
    property real lastY: 0

    onPressed: {
      lastX = mouse.x;
      lastY = mouse.y;
    }

    onPositionChanged: {
      if (!(mouse.buttons & Qt.LeftButton)) return;
      var dx = mouse.x - lastX;
      var dy = mouse.y - lastY;
      lastX = mouse.x;
      lastY = mouse.y;
      var target = map.toCoordinate(Qt.point(map.width / 2 - dx,
                                             map.height / 2 - dy));
      map.center = target;
    }
  }

  WheelHandler {
    id: wheelZoom
    target: root
    onWheel: {
      var step = wheel.angleDelta.y > 0 ? 1 : -1;
      map.zoomLevel = Math.max(map.minimumZoomLevel,
                               Math.min(map.maximumZoomLevel, map.zoomLevel + step));
      wheel.accepted = true;
    }
  }

  Rectangle {
    id: zoomPanel
    width: 28
    height: 60
    radius: 4
    color: "#2B2B2B"
    opacity: 0.8
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.margins: 8

    Column {
      anchors.centerIn: parent
      spacing: 6

      Rectangle {
        width: 22
        height: 22
        radius: 3
        color: "#3A3A3A"
        Text { anchors.centerIn: parent; text: "+"; color: "white"; font.pixelSize: 16 }
        MouseArea {
          anchors.fill: parent
          onClicked: map.zoomLevel = Math.min(map.maximumZoomLevel, map.zoomLevel + 1)
        }
      }

      Rectangle {
        width: 22
        height: 22
        radius: 3
        color: "#3A3A3A"
        Text { anchors.centerIn: parent; text: "-"; color: "white"; font.pixelSize: 16 }
        MouseArea {
          anchors.fill: parent
          onClicked: map.zoomLevel = Math.max(map.minimumZoomLevel, map.zoomLevel - 1)
        }
      }
    }
  }
}
