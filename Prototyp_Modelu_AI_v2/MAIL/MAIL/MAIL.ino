/*
  DZIUPLA VISION - MAIL
  Board: Seeed XIAO ESP32-S3
  Receives classification jobs from SVR on 192.168.4.12.
  Only positive "list" result triggers email.
  Mail is sent via Brevo HTTPS API after switching from DZIUPLA-VISION
  to phone hotspot IPPM. After sending, the board rejoins DZIUPLA-VISION.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

const char* LOCAL_SSID="DZIUPLA-VISION";
const char* LOCAL_PASSWORD="Dziupla2026";
const char* HOTSPOT_SSID="IPPM";
const char* HOTSPOT_PASSWORD="UZUPELNIJ_HASLO_HOTSPOTU"; // wpisz haslo hotspotu telefonu
const IPAddress MAIL_IP(192,168,4,12), GATEWAY(192,168,4,1), SUBNET(255,255,255,0);

// Brevo: wygeneruj API key v3 i użyj zweryfikowanego nadawcy.
const char* BREVO_API_KEY="UZUPELNIJ_BREVO_API_KEY";
const char* SENDER_EMAIL="UZUPELNIJ_ZWERYFIKOWANY_ADRES_NADAWCY";
const char* SENDER_NAME="Dziupla Vision";

WebServer web(80);
struct MailTask { String email; String label; float confidence; bool isLetter; bool valid; };
MailTask pending={"","",0.0f,false,false};
bool taskPending=false, busy=false;
uint32_t lastHeartbeat=0, heartbeatCount=0, sentCount=0;
String lastState="Uruchamianie";

String jsonStringField(const String& json,const String& key) {
  String needle="\""+key+"\"";
  int p=json.indexOf(needle); if(p<0)return "";
  p=json.indexOf(':',p+needle.length()); if(p<0)return "";
  p=json.indexOf('"',p+1); if(p<0)return "";
  int e=p+1; bool esc=false;
  for(;e<(int)json.length();e++){char c=json[e];if(c=='"'&&!esc)break;if(c=='\\'&&!esc)esc=true;else esc=false;}
  if(e>=(int)json.length())return "";
  String out=json.substring(p+1,e); out.replace("\\\"","\""); out.replace("\\\\","\\");
  return out;
}
bool jsonBoolField(const String& json,const String& key) {
  String needle="\""+key+"\""; int p=json.indexOf(needle); if(p<0)return false;
  p=json.indexOf(':',p+needle.length()); if(p<0)return false;
  String v=json.substring(p+1); v.trim(); return v.startsWith("true");
}
float jsonFloatField(const String& json,const String& key) {
  String needle="\""+key+"\""; int p=json.indexOf(needle); if(p<0)return 0;
  p=json.indexOf(':',p+needle.length()); if(p<0)return 0;
  return json.substring(p+1).toFloat();
}
String jsonEscapeMail(const String& s) {
  String o; for(size_t i=0;i<s.length();i++){char c=s[i];if(c=='"'||c=='\\'){o+='\\';o+=c;}else if(c=='\n')o+="\\n";else if(c=='\r')o+="\\r";else if((uint8_t)c>=32)o+=c;}return o;
}
bool validEmail(const String& e) {
  int a=e.indexOf('@'),d=e.lastIndexOf('.');
  return e.length()>4&&e.length()<101&&a>0&&d>a+1&&d<(int)e.length()-1;
}
void logToSVR(const String& message) {
  if(WiFi.status()!=WL_CONNECTED)return;
  HTTPClient h; h.setTimeout(3000);
  h.begin(String("http://192.168.4.1/api/log?device=MAIL&message=")+
    message); // messages used below are URL-safe
  h.GET(); h.end();
}
void heartbeat() {
  if(WiFi.status()!=WL_CONNECTED)return;
  HTTPClient h; h.setTimeout(2000);
  h.begin("http://192.168.4.1/api/heartbeat?device=MAIL&message=MAIL%20ready");
  int c=h.GET(); if(c>0)heartbeatCount++; h.end(); lastHeartbeat=millis();
}
void handleTask() {
  String body=web.arg("plain");
  String email=jsonStringField(body,"email");
  String label=jsonStringField(body,"label");
  float confidence=jsonFloatField(body,"confidence");
  bool isLetter=jsonBoolField(body,"is_letter");
  if(!validEmail(email)||label.length()==0) {
    web.send(400,"application/json","{\"ok\":false,\"error\":\"invalid task data\"}"); return;
  }
  if(taskPending||busy) {
    web.send(409,"application/json","{\"ok\":false,\"error\":\"MAIL busy\"}"); return;
  }
  pending.email=email; pending.label=label; pending.confidence=confidence;
  pending.isLetter=isLetter; pending.valid=true; taskPending=true;
  lastState="Zadanie odebrane od SVR";
  web.send(202,"application/json","{\"ok\":true,\"queued\":true}");
}
void handleStatus() {
  String j="{\"device\":\"MAIL\",\"status\":\"online\",\"ip\":\""+WiFi.localIP().toString()+
    "\",\"heartbeats\":"+String(heartbeatCount)+",\"sent\":"+String(sentCount)+
    ",\"busy\":"+(busy?"true":"false")+",\"last_state\":\""+jsonEscapeMail(lastState)+"\"}";
  web.send(200,"application/json",j);
}
bool sendBrevo(const MailTask& t,String& responseText) {
  if(String(BREVO_API_KEY).startsWith("UZUPELNIJ")||
     String(SENDER_EMAIL).startsWith("UZUPELNIJ")) {
    responseText="Uzupelnij BREVO_API_KEY i SENDER_EMAIL w MAIL.ino";
    return false;
  }
  WiFiClientSecure client; client.setInsecure();
  HTTPClient https; https.setTimeout(20000);
  if(!https.begin(client,"https://api.brevo.com/v3/smtp/email")) {
    responseText="Nie mozna uruchomic HTTPS"; return false;
  }
  https.addHeader("api-key",BREVO_API_KEY);
  https.addHeader("Content-Type","application/json");
  String subject="Dziupla Vision - wykryto list";
  String html="<html><body><h2>Wykryto list</h2><p>System Dziupla Vision rozpoznal list.</p><p>Wynik AI: "+
    t.label+"</p><p>Pewnosc: "+String(t.confidence*100.0f,1)+"%</p></body></html>";
  String payload="{\"sender\":{\"name\":\""+jsonEscapeMail(SENDER_NAME)+"\",\"email\":\""+
    jsonEscapeMail(SENDER_EMAIL)+"\"},\"to\":[{\"email\":\""+jsonEscapeMail(t.email)+"\"}],"+
    "\"subject\":\""+subject+"\",\"htmlContent\":\""+jsonEscapeMail(html)+"\"}";
  int code=https.POST(payload);
  responseText=https.getString();
  https.end();
  return code>=200&&code<300;
}
void processTask() {
  if(!taskPending||busy)return;
  busy=true; taskPending=false;
  MailTask task=pending; pending.valid=false;
  if(!task.isLetter || task.label!="list" || task.confidence<0.65f) {
    lastState="AI nie rozpoznalo listu - e-mail nie zostal wyslany";
    logToSVR("AI%20result%20not%20a%20letter");
    busy=false; return;
  }
  lastState="Laczenie z hotspotem IPPM";
  WiFi.disconnect(true); delay(300); WiFi.mode(WIFI_STA);
  WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0));
  WiFi.begin(HOTSPOT_SSID,HOTSPOT_PASSWORD);
  uint32_t start=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<25000) delay(250);
  String result;
  bool ok=false;
  if(WiFi.status()==WL_CONNECTED) {
    lastState="Wysylanie e-mail przez Brevo";
    ok=sendBrevo(task,result);
  } else result="Nie udalo sie polaczyc z hotspotem IPPM";
  if(ok) {sentCount++;lastState="E-mail wyslany do "+task.email;}
  else lastState="Blad wysylania: "+result.substring(0,100);

  // Return to local SVR network to report completion.
  WiFi.disconnect(true); delay(300); WiFi.config(MAIL_IP,GATEWAY,SUBNET,GATEWAY);
  WiFi.begin(LOCAL_SSID,LOCAL_PASSWORD);
  start=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<25000) delay(250);
  if(WiFi.status()==WL_CONNECTED) {
    String safe=ok?"MAIL%20sent%20email":"MAIL%20email%20failed";
    logToSVR(safe);
    heartbeat();
  }
  busy=false;
}
void setup() {
  Serial.begin(115200); delay(500);
  WiFi.persistent(false); WiFi.setSleep(false); WiFi.mode(WIFI_STA);
  WiFi.config(MAIL_IP,GATEWAY,SUBNET,GATEWAY); WiFi.begin(LOCAL_SSID,LOCAL_PASSWORD);
  Serial.print("[MAIL] Connecting to SVR");
  uint32_t t=millis(); while(WiFi.status()!=WL_CONNECTED&&millis()-t<20000){delay(300);Serial.print(".");}
  Serial.println();
  web.on("/api/task",HTTP_POST,handleTask);
  web.on("/api/status",HTTP_GET,handleStatus);
  web.begin();
  Serial.printf("[MAIL] IP: %s\n",WiFi.localIP().toString().c_str());
  heartbeat();
}
void loop() {
  web.handleClient();
  if(WiFi.status()==WL_CONNECTED && millis()-lastHeartbeat>5000){lastHeartbeat=millis();heartbeat();}
  if(taskPending&&!busy) processTask();
  if(WiFi.status()!=WL_CONNECTED&&!busy) {
    static uint32_t retry=0;
    if(millis()-retry>5000){retry=millis();WiFi.config(MAIL_IP,GATEWAY,SUBNET,GATEWAY);WiFi.begin(LOCAL_SSID,LOCAL_PASSWORD);}
  }
  delay(2);
}
