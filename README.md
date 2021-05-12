# hw3

Setup
                
    1.Download Files or git clone https://github.com/yansin1198/hw3.git
    2.In the file, add "4DGL-uLCD-SE" library: 
      git clone https://gitlab.larc-nthu.net/ee2405_2021/4dgl-ulcd-se.git
    3.In the file, add a library for decoding: (BSP library is also needed)
      mbed add https://gitlab.larc-nthu.net/ee2405_2021/tensorflowlite_mbed
      $ cp -r ~/ee2405/<library路徑>/src/data_collect/BSP_B-L475E-IOT01.
    4.In the file, add RPC Library: 
      mbed add https://gitlab.larc-nthu.net/ee2405_2021/mbed_rpc.git
    5.In the file, add MQTT: 
      mbed add https://gitlab.larc-nthu.net/ee2405_2019/wifi_mqtt.git
    6.Modify mbed_app.json contents (SSID、PSSWORD)
    7.Modify main.cpp and mqtt_client.py IP address
    
Compile
    
    1. Open terminal:
       $ mkdir -p ~/ee2405new
    2. cp -r ~/ee2405/mbed-os ~/ee2405new
    3. mbed compile --library --no-archive -t GCC_ARM -m B_L4S5I_IOT01A --build ~/ee2405new/mbed-os-build2 (這一步要在ee2405new裡面做)
    4. sudo mbed compile --source . --source ~/ee2405new/mbed-os-build2/ -m B_L4S5I_IOT01A -t GCC_ARM -f 去compile作業3
    5. sudo screen /dev/ttyACM0
    6. Open the other terminal to finish connect:
       sudo python3 wifi_mqtt/mqtt_client.py
    
How to run

    1.RPC UI command: 
      /gestureControl/run 1
    2.LED1亮起
    3.手勢辨識(垂直方向畫直線)選擇angles (30, 45, 60, 90)
    4.按下user button確定angle
    5.LED1熄滅，同時傳送目前選定的angle到PC，並返回RPC loop

    6.RPC Angle command: 
      /tiltAngleControl/run 1
    7.注意LED2會先閃兩下表示初始化的座標過程完成
    8.閃完兩下後，LED2亮起
    9.開始測量角度是否超過選擇的angle
    10.如果超過，LED3會亮
    11.同時測量到的角度會傳到PC
    12.待PC收到10個events之後，LED2及LED3熄滅，結束這個mode，返回RPC loop

Results

    1.After type RPC UI command: /gestureControl/run 1
   ![image](https://user-images.githubusercontent.com/59250738/117927381-b74ad600-b32c-11eb-879b-87f3af5cd9a3.png)

    2.After use gesture to choose angle
   ![image](https://user-images.githubusercontent.com/59250738/117927436-ca5da600-b32c-11eb-8378-78eeebd8dd75.png)

    3.After press user button
   ![image](https://user-images.githubusercontent.com/59250738/117927474-d47fa480-b32c-11eb-9d10-987680104c4f.png)

    4.Back to RPC loop, then type RPC Angle command: /tiltAngleControl/run 1
   ![image](https://user-images.githubusercontent.com/59250738/117927518-e3feed80-b32c-11eb-9844-fc1aa0917bee.png)

    5.After tilt mbed
   ![image](https://user-images.githubusercontent.com/59250738/117927544-ecefbf00-b32c-11eb-933a-f4ce95e6301a.png)

    6.When tilt angle larger than choose angle, send tilt angle to PC
   ![image](https://user-images.githubusercontent.com/59250738/117927562-f2e5a000-b32c-11eb-9ad3-af9e3ecb0930.png)

    7.After get 11 events (number 0~10 event), back to RPC loop

    

