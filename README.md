# ESP32 GPS Test

## Description
ZED-F9P 칩셋 GPS 데이터 테스트   
W5500 Ethernet 통신 테스트   

## Commit
2023.04.28 - NMEA-0183 Protocol GNRMC parsing    
2023.05.10 - ESP32-S3-DevkitC-1 Board LED strip component 추가     
2023.05.20 - d9 Lib 적용     
2023.06.02 - Test Code 완료     

## Test Spec
```
Chip : ESP32-S3-WROOM-1 -> ESP32-WROOM-32D
Board : ESP32-S3-DevkitC-1 -> blank Board
Ethernet : W5500io (W5500칩 기반 모듈)
micro SD : SPI
GPS Module : SparkFun GPS-RTK2 Board - ZED-F9P (Qwiic)
Antenna : ANN-MB1
Debuger : ESP-Prog
```    

[Dev91 Blog](https://dev91.tistory.com/)

## Image

![ESP32_GPS_TEST](https://github.com/Dev-91/ESP32_GPS_TEST/assets/38420069/0a4b2af0-8bab-4ac1-a4e3-6b8d97fe36a9)
