
<div align="center">
  <h1>SR24 OLED Projekt
</div>
Dokumentation für den Anschluss eines Raspberry Pico mit Display am Steuergerät des Umbausatzes SR24 von Second Ride. Wenn der Motor an ist, werden vom Pico sämtliche Daten ausgelesen, die der Controller über USB seriell sendet.

Folgende Daten werden auf dem Display dargestellt:
- Geschwindigkeit
- Akkutemperatur
- Restreichweite

Optional kann ein Taster angeschlossen werden, um weitere Bildschirmausgaben durchschalten zu können

Bildschirm 2
- Balken für die Momentanleistung
- Verbrauch
- Momentanleistung

Bildschirm 3
- Spannung
- Strom

Weiterhin kann optional ein GPS-Modul angeschlossen werden. Folgende zusätlichen Werte werden dann angezeigt:
- Uhrzeit
- Geschwindigkeit via GPS (nur wenn Satteliten verfügbar sind)
- Anzahl der ausgewerteten Satteliten

# Teileliste

Hier ist der angeforderte Beispieltext mit HTML-Formatierung:

<div align="center">
  <h3>Willkommen beim SR24 Dashboard</h3>
  <p>
    Dieser Text wurde mit <strong>HTML-Tags</strong> formatiert.<br>
    Er ist zentriert und enthält einen <a href="https://github.com/olikraus/u8g2">Link zur u8g2 Bibliothek</a>.
  </p>
</div>

![Dashboard Screenshot](raw/Screenshot%202026-02-21%20214236.png)