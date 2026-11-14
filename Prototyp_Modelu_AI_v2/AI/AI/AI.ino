#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ListRecognition_inferencing.h>

const char* SSID="DZIUPLA-VISION"; const char* PASS="Dziupla2026";
const IPAddress AI_IP(192,168,4,11), GW(192,168,4,1), MASK(255,255,255,0);
const char* SVR_HOST="192.168.4.1";
static constexpr size_t W=EI_CLASSIFIER_INPUT_WIDTH, H=EI_CLASSIFIER_INPUT_HEIGHT, PIXELS=W*H, IMAGE_BYTES=PIXELS*3;
static constexpr float THRESHOLD=0.65f;
static_assert(W==240 && H==240,"Model musi miec wejscie 240x240");
WiFiServer httpServer(80); uint8_t* imageBuf=nullptr; String lastLabel="unknown"; float lastConfidence=0; uint32_t countClass=0;
String recipient=""; uint32_t lastHeartbeat=0;
void logSVR(const String& msg,bool error=false){if(WiFi.status()!=WL_CONNECTED)return;WiFiClient c;if(!c.connect(SVR_HOST,80))return;String body="{\"device\":\"AI\",\"level\":\""+(error?String("error"):String("info"))+"\",\"message\":\""+msg+"\"}";c.print("POST /api/log HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/json\r\nContent-Length: "+String(body.length())+"\r\nConnection: close\r\n\r\n"+body);uint32_t t=millis();while(c.connected()&&millis()-t<1000){while(c.available())c.read();delay(1);}c.stop();}
void heartbeatAI(){
  if(WiFi.status()!=WL_CONNECTED) return;
  HTTPClient h; h.setTimeout(2000);
  h.begin("http://192.168.4.1/api/heartbeat?device=AI&message=AI%20ready");
  h.GET(); h.end(); lastHeartbeat=millis();
}
static int signalData(size_t offset,size_t length,float*out){if(!imageBuf||offset+length>PIXELS)return -1;for(size_t i=0;i<length;i++){size_t p=(offset+i)*3;out[i]=(float)(((uint32_t)imageBuf[p]<<16)|((uint32_t)imageBuf[p+1]<<8)|imageBuf[p+2]);}return 0;}
String readLine(WiFiClient& c,uint32_t timeout=10000){String s="";uint32_t start=millis();while(millis()-start<timeout){while(c.available()){char ch=c.read();if(ch=='\n')return s;if(ch!='\r')s+=ch;}if(!c.connected())break;delay(1);}return s;}
void sendResponse(WiFiClient& c,int code,const char* status,const String& body){c.print("HTTP/1.1 ");c.print(code);c.print(" ");c.print(status);c.print("\r\nContent-Type: application/json\r\nContent-Length: ");c.print(body.length());c.print("\r\nConnection: close\r\n\r\n");c.print(body);}
void reportResultToSVR(const String& json){
  if(WiFi.status()!=WL_CONNECTED) return;
  HTTPClient http; http.setTimeout(8000);
  if(!http.begin("http://192.168.4.1/api/result")) { logSVR("SVR_RESULT_CONNECT_FAILED",true); return; }
  http.addHeader("Content-Type","application/json");
  int code=http.POST(json);
  Serial.printf("[AI] Result forwarded to SVR, HTTP %d\\n",code);
  http.end();
  if(code<200 || code>=300) logSVR("SVR_RESULT_FORWARD_FAILED HTTP="+String(code),true);
}
void handleClient(WiFiClient client){client.setTimeout(12000);String request=readLine(client);if(!request.startsWith("POST /api/classify ")){sendResponse(client,404,"Not Found","{\"error\":\"NOT_FOUND\"}");client.stop();return;}size_t contentLength=0;recipient="";bool headersDone=false;uint32_t headerStart=millis();while(client.connected()&&millis()-headerStart<10000){String h=readLine(client);if(h.length()==0){headersDone=true;break;}String lower=h;lower.toLowerCase();if(lower.startsWith("content-length:"))contentLength=h.substring(h.indexOf(':')+1).toInt();if(lower.startsWith("x-recipient-email:"))recipient=h.substring(h.indexOf(':')+1);recipient.trim();}
 if(!headersDone){sendResponse(client,400,"Bad Request","{\"error\":\"BAD_HEADERS\"}");client.stop();logSVR("BAD_HEADERS",true);return;}
 Serial.printf("[AI] Content-Length: %u\n",(unsigned)contentLength);Serial.printf("[AI] Recipient: %s\n",recipient.c_str());
 if(contentLength!=IMAGE_BYTES){String j="{\"error\":\"IMAGE_SIZE_MISMATCH\",\"expected\":"+String(IMAGE_BYTES)+",\"received\":"+String(contentLength)+"}";sendResponse(client,400,"Bad Request",j);client.stop();logSVR("IMAGE_SIZE_MISMATCH expected="+String(IMAGE_BYTES)+" received="+String(contentLength),true);return;}
 size_t pos=0;uint32_t lastData=millis();while(pos<IMAGE_BYTES&&client.connected()){int avail=client.available();if(avail>0){size_t n=(size_t)avail;if(n>IMAGE_BYTES-pos)n=IMAGE_BYTES-pos;int got=client.read(imageBuf+pos,n);if(got>0){pos+=got;lastData=millis();}}else{if(millis()-lastData>15000)break;delay(1);}}
 Serial.printf("[AI] Odebrano: %u/%u\n",(unsigned)pos,(unsigned)IMAGE_BYTES);
 if(pos!=IMAGE_BYTES){String j="{\"error\":\"INCOMPLETE_IMAGE\",\"expected\":"+String(IMAGE_BYTES)+",\"received\":"+String(pos)+"}";sendResponse(client,400,"Bad Request",j);client.stop();logSVR("INCOMPLETE_IMAGE received="+String(pos),true);return;}
 logSVR("IMAGE_RECEIVED bytes=172800");Serial.println("[AI] CLASSIFICATION_STARTED");logSVR("CLASSIFICATION_STARTED");signal_t signal;signal.total_length=PIXELS;signal.get_data=signalData;ei_impulse_result_t result={0};EI_IMPULSE_ERROR err=run_classifier(&signal,&result,false);if(err!=EI_IMPULSE_OK){String j="{\"error\":\"CLASSIFICATION_FAILED\",\"code\":"+String((int)err)+"}";sendResponse(client,500,"Internal Server Error",j);client.stop();logSVR("CLASSIFICATION_FAILED code="+String((int)err),true);return;}
 String best="unknown";float confidence=0;for(size_t i=0;i<EI_CLASSIFIER_LABEL_COUNT;i++){float v=result.classification[i].value;Serial.printf("[AI] %s: %.5f\n",result.classification[i].label,v);if(v>confidence){confidence=v;best=String(result.classification[i].label);}}
 bool isLetter=(best=="list"&&confidence>=THRESHOLD);lastLabel=best;lastConfidence=confidence;countClass++;String json="{\"email\":\""+recipient+"\",\"label\":\""+best+"\",\"confidence\":"+String(confidence,5)+",\"is_letter\":"+(isLetter?"true":"false")+"}";
 sendResponse(client,200,"OK",json);client.flush();delay(20);client.stop();Serial.println("[AI] JSON_RESPONSE_SENT: "+json);logSVR("CLASSIFICATION_FINISHED label="+best+" confidence="+String(confidence,5));reportResultToSVR(json);logSVR("JSON_RESPONSE_SENT");
}
void setup(){Serial.begin(115200);delay(1200);Serial.println("[AI] DZIUPLA VISION AI START");imageBuf=(uint8_t*)(psramFound()?ps_malloc(IMAGE_BYTES):malloc(IMAGE_BYTES));if(!imageBuf){Serial.println("[AI] FATAL: BUFFER ALLOCATION FAILED");while(true)delay(1000);}Serial.printf("[AI] Buffer: %u bytes\n",(unsigned)IMAGE_BYTES);WiFi.persistent(false);WiFi.setSleep(false);WiFi.mode(WIFI_STA);WiFi.config(AI_IP,GW,MASK,GW);WiFi.begin(SSID,PASS);Serial.print("[AI] Connecting");uint32_t t=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-t<20000){delay(500);Serial.print('.');}Serial.println();if(WiFi.status()==WL_CONNECTED){Serial.print("[AI] IP: ");Serial.println(WiFi.localIP());}else Serial.printf("[AI] WIFI STATUS: %d\n",(int)WiFi.status());httpServer.begin();Serial.println("[AI] Endpoint: POST /api/classify");heartbeatAI();}
uint32_t reconnectAt=0;void loop(){if(WiFi.status()!=WL_CONNECTED){if(millis()-reconnectAt>5000){reconnectAt=millis();Serial.println("[AI] WiFi reconnect");WiFi.begin(SSID,PASS);}delay(10);return;}if(millis()-lastHeartbeat>5000)heartbeatAI();WiFiClient c=httpServer.available();if(c)handleClient(c);delay(1);}
