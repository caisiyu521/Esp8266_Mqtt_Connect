#include <Ticker.h>  //导入定时器库
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Arduino_JSON.h>

Ticker tciker;  //实例化定时器对象
Ticker tciker1;  //实例化定时器对象
int count = 0;
// const char* mqtt_server = "118.178.59.37";

WiFiClient espClient;
PubSubClient client(espClient);
String Usart_Receive_Buf = "";
char msgJson[200];  //存储json数据
bool Mqtt_Config_ok = 0;


struct Config_mqtt_struct {  //结构体存放账号密码主题和消息
   char ssid[32];
   char password[32];
};
Config_mqtt_struct mqtt_struct;

long lastMsg_time = 0;

char charArray[50];
String mqtt_server;
String mqtt_username;
String mqtt_client_id;
String mqtt_password;
char esp8266_state_code = '0';      //0:设备还没有连上网路  1：设备已经连上热点

void setup() { 
    delay(2000);

    
    Serial.begin(115200);
    // printf_begin();

    pinMode(LED_BUILTIN, OUTPUT);
    EEPROM.begin(512);

    Serial.println();
    setup_wifi();

    // tciker1.attach_ms(2000, mqtt_reconnect_callback);            //每隔1秒执行一次回调函数
}

void loop() { 

    while (Serial.available() > 0)  {
        Usart_Receive_Buf += char(Serial.read());
        delay(2);
    }
    if (Usart_Receive_Buf.length() > 0){
        // Serial.println(Usart_Receive_Buf);
        if(Usart_Receive_Buf.equals("Connect_OK?")){
            Serial_Send_Mcu("reply<Connect_OK>");
        }
        else if(Usart_Receive_Buf.equals("Inin_OK?")){
            if(esp8266_state_code == '2'){
                Serial_Send_Mcu("reply<INIT_OK>");
            }
            else if(esp8266_state_code == '0'){
                Serial_Send_Mcu("reply<INIT_AP_Fail>");
            }
            else if(esp8266_state_code == '1'){
                Serial_Send_Mcu("reply<INIT_Mqtt_Fail>");
            }
        }
        else {
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, Usart_Receive_Buf);
            JsonObject obj = doc.as<JsonObject>();
            if ((obj[String("Pub")] || obj[String("Sub")]) && (obj[String("Author")] == "caisiyu") && obj[String("Psaaword")] && obj[String("User")]  && obj[String("Topic")] && obj[String("Message")] && obj[String("Sever")] && obj[String("Client_id")]) {
                // Serial.println("<<<<<<Shibie OK :");
                if (client.connected()) {
                    // Serial.print("<<<<<< client.connected :");
                    if(obj[String("Pub")]){
                        client.publish(obj[String("Topic")],obj[String("Message")]);
                        // Serial.print("<<<<<<Pub OK :");
                        Serial_Send_Mcu("reply<pub_ok>");
                    }
                    else if(obj[String("Sub")]){
                        client.subscribe(obj[String("Topic")]);
                        // Serial.print("<<<<<<Sub OK :");
                        Serial_Send_Mcu("reply<sub_ok>");
                    }
                }
                else if (!client.connected()){
                    Mqtt_Config_ok = 1;
                    mqtt_server = obj["Sever"].as<String>();
                    mqtt_client_id = obj["Client_id"].as<String>();
                    mqtt_username = obj["User"].as<String>();
                    mqtt_password = obj["Psaaword"].as<String>();
                }
            }
            else {
                Serial_Send_Mcu("reply<String_style_fail>");
            }
        }
        Usart_Receive_Buf = "";
    }

    if (!client.connected()){
        esp8266_state_code = '1';
        mqtt_reconnect_callback();
    }
    else {
    //    tciker1.detach(); //停止当前任务
    }
    
    
    if ( WiFi.status() != WL_CONNECTED) {
        esp8266_state_code = '0';
        setup_wifi();//让esp8266接入热点
    }else {
        // esp8266_state_code = '1';
    }
    client.loop();
}

void setup_wifi(){
    tciker.attach_ms(500, Led_callback);            //每隔1秒执行一次回调函数
    
    EEPROM.get<Config_mqtt_struct>(0, mqtt_struct);
    delay(1000);
     Serial.print("<<<<<<EEPROM storage for  ssid :");
     Serial.println(mqtt_struct.ssid);
     Serial.print("<<<<<<EEPROM storage for  password :");
     Serial.println(mqtt_struct.password);

    // Serial.println("<<<<<<Use EEPROM for storage");
    WiFi.begin(mqtt_struct.ssid, mqtt_struct.password);
    lastMsg_time = millis();
    while (WiFi.status() != WL_CONNECTED) {//正在连接wifi
        delay(500);
        long now = millis();
        if (now - lastMsg_time > 10000) {//wifi 连接超时
            // Serial.println("<<<<<<Use EEPROM connection timeout");
            smartConfig();  //微信智能配网
            break;
        }
    }
    ESP8266_Connect_AP_success();
}

/**
 * MQTT客户端断线重连函数
 * 
 * Mqtt_Config_ok 代表 mqtt的信息已经 通过串口获得啦"mqtt_server 之类的有值"
 */
void mqtt_reconnect_callback() {
    // Serial.println( "<<<<<<MQTT turn connection mqtt_Server !" );
    if(Mqtt_Config_ok == 1){
        client.setServer(mqtt_server.c_str(), 1883);    //设置服务器 ip 和端口
        client.setCallback(callback);   //设置 mqtt接受函数的回调函数
        while (!client.connected()) {   //如果没有连接
            // 尝试连接connect是个重载函数 (clientId, username, password)
            if (client.connect(mqtt_client_id.c_str(),mqtt_username.c_str(),mqtt_password.c_str())  ) {
                // Serial.println( "<<<<<<MQTT turn connection mqtt_Server success!" );
                Serial_Send_Mcu("<esp8266_init_ok>");//打印esp8266准备好的消息
                Clear_Serial_Buffer();              //清空串口中缓存的数据
                esp8266_state_code = '2';           //状态 已经连接mqtt 所以置2
            } else {
                // Serial.println( "<<<<<<MQTT turn connection failed!" );
                delay(2000);            
            }
        }
    }
}

/**
 * MQTT客户端 获得消息 的回调函数callback
 * 
 * Mqtt_Config_ok 代表 mqtt的信息已经 通过串口获得啦"mqtt_server 之类的有值"
 */
void callback(char* topic, byte* payload, unsigned int length) {
    int i = 0;
    char string[50];
    // Serial.print("Message arrived [");
    // Serial.print(topic);
    // Serial.print("] ");
    for (i = 0; i < length; i++) {      //将获得数据 放到string中 缓存
        string[i] = (char)payload[i];
    }
    string[i] = '\0';
    // Serial.println(string);

    //将数据封装成json字符串
    //例子： receive<{"topic":"/caisiyu","message":"\"wendu\":\"23\",\"shidu\":\"68\""}>
    JSONVar myObject;               
    myObject["topic"] = topic;
    myObject["message"] = string;
    String jsonString = JSON.stringify(myObject);
    jsonString = "receive<"+jsonString+">";
    Serial_Send_Mcu(jsonString);

    // string = "";
}

/**
 * 配网 功能
 * 
 * 
 */
void smartConfig(){
    // Serial.println("<<<<<<Use smart distribution network");
    tciker.attach_ms(100, Led_callback);            //每隔1秒执行一次回调函数
    WiFi.mode(WIFI_STA);
    WiFi.beginSmartConfig();        //开始配网
    // Serial.print("In distribution network:");
    // 收到配网信息后ESP8266将自动连接，WiFi.status 状态就会返回：已连接
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        // Serial.print(".");//完成连接，退出配网等待。
        // Serial.print(WiFi.smartConfigDone());
    }
    Serial.println("");

//    EEPROM.begin(512);
    
    strcpy(mqtt_struct.ssid, WiFi.SSID().c_str());      //将ap信息 存储到eeprom中
    mqtt_struct.ssid[sizeof(mqtt_struct.ssid+0)] = '\0';
    strcpy(mqtt_struct.password, WiFi.psk().c_str());   //
    mqtt_struct.password[sizeof(mqtt_struct.password+0)] = '\0';
    EEPROM.put<Config_mqtt_struct>(0, mqtt_struct);     //将mqtt_struct 中的数据 存到地址0起始的位置
    EEPROM.commit();        //提交
    
    ESP8266_Connect_AP_success();       //设置小灯闪烁
}

void  Serial_Send_Mcu(String buf){
    Serial.println(buf);
}

void Led_callback() {     //回调函数
    static boolean output=HIGH; 
    digitalWrite(LED_BUILTIN, !output);  
    output=!output;
}

// void Mqtt_Pub_callback() {     //回调函数
//     static boolean output=HIGH; 
//     digitalWrite(LED_BUILTIN, !output);  
//     output=!output;
// }

void Clear_Serial_Buffer(){
    while(Serial.available() > 0)
        Serial.read();
}

void ESP8266_Connect_AP_success(){
    // Serial.println("<<<<<<WiFi connected!   ");
    // Serial.print("<<<<<<IP address: ");
    // Serial.println(WiFi.localIP());
    // Serial.print("<<<<<<WIFI Name : ");
    // Serial.println(WiFi.SSID());
    
    tciker.attach_ms(2000, Led_callback);            //每隔2秒执行一次回调函数
}
