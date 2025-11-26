/* esp32_color_dispense.ino
   Full single-file sketch for:
   - TCS3200 color sensor
   - SG90 servo dispenser
   - Active buzzer (non-blocking alarm)
   - DS3231 RTC
   - AWS IoT Shadow integration over TLS using embedded certificate strings
   - Publishes reported state and handles delta for desired.color, pill_hour, pill_minute, buzzer_enabled
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <RTClib.h>

// =========== CONFIG - rellenar =============
const char* WIFI_SSID = "FLIA BAVING";
const char* WIFI_PASS = "7375388MIRDZAAUZA";

const char* AWS_IOT_ENDPOINT = "a4ni87v7kq10b-ats.iot.us-east-2.amazonaws.com";
const int AWS_IOT_PORT = 8883;
const char* THING_NAME = "esp32-color-shadow";
const char* CLIENT_ID = "esp32-color-shadow-CALEB-BAVING";
// ===========================================

// ---------- AWS Certs embedded (replace with your actual PEM content) ----------
const char* rootCA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

const char* deviceCert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUd4pztSyU9zDw9CnZqia03LUmD8MwDQYJKoZIhvcNAQEL
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI1MTEwNzE1NDUx
M1oXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALcCAbAHaQRHVrsyF240
BOuZ4TFs0M65e01j7G1iygR1M7izEVHKhgF+7up7pVgNh5rAoHyOuKNJ9H9EmddT
8FcZaFvW8Fh2857AC+FxmjViu+o3iIwrRnaquQgkGP04CFqmxOP8S6rKvbtdt4N6
DzSSxP9acNLbKOdXTCIqDkeXKmCtyLUS8ic4YTry3ilTKHOGYBWUgu0HtjaPFmx1
l/jTmJAs+4E2BI1touEapd2IlqD9785WCr4wbrtjk+b1xwaIiUKKK6QSi8J3II4s
9VXkjeRf8sB13A5BznqBHYyKIMd+oh5Hk3P0EDSNh3dQsif4Y8D3CrHD3OAWBltw
Zh8CAwEAAaNgMF4wHwYDVR0jBBgwFoAU+tMnlb4BpsxIY0pYbiLi1g+yrXkwHQYD
VR0OBBYEFCjL+pfCaFgsd4pwi7E4MoxQWD8/MAwGA1UdEwEB/wQCMAAwDgYDVR0P
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQBoV+LLrZ9kQWSCjlHzArvF+kD0
crbfkMit07nkUM6Gj58+MsK5GHFKskqjVKRpJQj3FCEEit+KPTEA00hReow/pbju
/R/YDaM5fl7B+QLa6PZP4c0fpxRZpuzfeM9sfqrlkYcSRZFjvlPDCps8l8YZHNig
YBwnkjKEL7KG1q7G6Mo0brCqRUtH2VdrJjJMdzj6043DF3yDhJ0J3Ys257tYFvfc
3EORbDP1lLtSJ6MXAYdA8xmf7szdzk45QyY/1V7EdFTGGLZAX8v2NFm9FnwUVs8Z
yQTrnCe3/bOiTc1DRsR55pcJnBDJc+DOm6+yOWmtge8jbV5JhFmf8ye++Xvr
-----END CERTIFICATE-----
)EOF";

const char* privateKey = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEAtwIBsAdpBEdWuzIXbjQE65nhMWzQzrl7TWPsbWLKBHUzuLMR
UcqGAX7u6nulWA2HmsCgfI64o0n0f0SZ11PwVxloW9bwWHbznsAL4XGaNWK76jeI
jCtGdqq5CCQY/TgIWqbE4/xLqsq9u123g3oPNJLE/1pw0tso51dMIioOR5cqYK3I
tRLyJzhhOvLeKVMoc4ZgFZSC7Qe2No8WbHWX+NOYkCz7gTYEjW2i4Rql3YiWoP3v
zlYKvjBuu2OT5vXHBoiJQoorpBKLwncgjiz1VeSN5F/ywHXcDkHOeoEdjIogx36i
HkeTc/QQNI2Hd1CyJ/hjwPcKscPc4BYGW3BmHwIDAQABAoIBAQCXQ3BTp/xUTgbR
GVkmfJaoifsJWDDK/aJ92B6+Vw41Ww5SFqg1G5lhuSIO6/5BZoV0Es1Txr+0L9eI
LhKeWUHpLBYG+wSTilZZG9F2GOjmQWKi+B3EBazrPrdLlFKXUe4Nx5QsAQgl9geW
y6J4aLYStVFg4scocX9An/ZMssg0wMWqAC9tZTKf8uHZY4ve5sI3gevUKPiHk0Uu
eMpqint0vE4amS4Rud4hMrDdXNEMQmrc4SENGVj3io9dUuYFjRjXMP+4ApVea+Iy
34GgKcYpGCvK3UKykDoK2nIEnVJ4rWCkrd4GOdEbnOAzIop6OKDeIUvJ4FAvW6FB
ppTEb4r5AoGBAOmf3v0TfoYsnQiz3CrMS82bS7rM+YONveL5UAp4qkXEbS9lZOes
5HjE0Q8bC1HozVQUKylnWIRMhI5/KI4gtFGYWGxA/KAWg2F+4vKExst/bjT/CH17
2kobMAoV+WtCaUkFTbqCSDplwXA//Hvp5L1Q99RPaOPRYdmqzx7Jbh1NAoGBAMiJ
Fuzze3WP/eDLYarLCojtFroRZdnZrpaaDzE3AdXAH7dQ3dxmLjmW8CG6DrYuBaB+
8EPohaMPaC1YLIHtgRA8rMeR/tQ/XMsBD/4d/PeLeCcxYI/0zj+TZ4eS/AtRKBAC
4wqXJQfxI1F/8g55ZIM6qYl88cA/0wVyZzbDVQsbAoGBAM8wKikVBctme2nBYMtP
3RYd2H500/+YT8OgSRzQQGmZNx+mc2OHECQOoD0eRd7BcH9VV6Xjcjv6REC/gq7x
UBlg22I+DAzJioCHcCuWF1tXytwTJWtr0H6SN/tp24YFIqxQmMuESRwJLBEpnfgi
yOogiXlvZ11LTtUkR4VNLGutAoGBAJBJIvemUKRL0E1XyJQMty3B+OIz9maCm328
p0Wv4GAddjR9uMQFuSiyk2CQ8FjgUCgkbVdPDChAw6IsmQl7C6vVHDQTtZidZnSh
9RHQHd02umLowiOR7nwL4SfI+BRkdkDe8uEB0yEdvV28gzsq2MkbAjTsczzyLzDy
GZVrgdsVAoGAeEZ9FQKFN+rBY0eS96BuTyeeZtr8GdI1rlD4y0DNGo/bh4Yva45D
k/YaoBHI6ZCbUd2Rtr7jbhyWmbX2NfTiYI3dL+9ctBEcz9okxjKdKB8OEdQg5YDK
wcK0duEqJT1vsXuPkfqFMQskWyjW+aMGMwbBqqlGvOr7wqYY2lEwMDs=
-----END RSA PRIVATE KEY-----
)EOF";

// Pins (según tu hardware)
const int PIN_S0 = 19;
const int PIN_S1 = 18;
const int PIN_S2 = 5;
const int PIN_S3 = 17;
const int PIN_OUT = 16;
const int PIN_SERVO = 13;
const int PIN_BUZZER = 12; // buzzer activo
// I2C: SDA 21, SCL 22 para DS3231

// Timing
const unsigned long COLOR_READ_DELAY = 80;
const unsigned long SERVO_MOVE_DELAY = 700;
const unsigned long DISPENSE_OPEN_MS = 1200;

// Angulos
namespace DispenseAngles {
  const int WHITE = 0;
  const int CREAM = 30;
  const int BROWN = 60;
  const int RED = 90;
  const int BLUE = 120;
  const int GREEN = 150;
  const int OTHER = 180;
  const int HOME = 90;
}

// ============ Globals ===========
WiFiClientSecure net;
PubSubClient client(net);
Servo servo;
RTC_DS3231 rtc;

unsigned long commandCounter = 0;
String updateTopic;
String deltaTopic;

// pill schedule (se actualizan desde Shadow delta)
int pillHour = -1;
int pillMinute = -1;
bool buzzerEnabled = false;
bool alarmTriggered = false;

// ---------- helper color struct ----------
struct RGB { uint8_t r,g,b; };

// ---------- TCS3200 init ----------
void tcsBegin() {
  pinMode(PIN_S0, OUTPUT); pinMode(PIN_S1, OUTPUT);
  pinMode(PIN_S2, OUTPUT); pinMode(PIN_S3, OUTPUT);
  pinMode(PIN_OUT, INPUT);
  digitalWrite(PIN_S0, HIGH); digitalWrite(PIN_S1, LOW); // freq scaling 20%
}

// raw read
int readRawColor(int s2_val, int s3_val) {
  digitalWrite(PIN_S2, s2_val); digitalWrite(PIN_S3, s3_val);
  delay(COLOR_READ_DELAY);
  unsigned long t = pulseIn(PIN_OUT, LOW, 30000);
  if(t == 0) return 30000;
  return (int)t;
}

RGB readAndNormalizeRGB() {
  int r_raw = readRawColor(LOW, LOW);
  int g_raw = readRawColor(HIGH, HIGH);
  int b_raw = readRawColor(LOW, HIGH);
  int maxv = max(max(r_raw, g_raw), b_raw);
  if(maxv == 0) maxv = 1;
  RGB out;
  out.r = constrain(map(r_raw, 0, maxv, 255, 0), 0, 255);
  out.g = constrain(map(g_raw, 0, maxv, 255, 0), 0, 255);
  out.b = constrain(map(b_raw, 0, maxv, 255, 0), 0, 255);
  return out;
}

// rgb -> hsv (h 0-360, s 0-255, v 0-255)
void rgbToHsv(uint8_t r,uint8_t g,uint8_t b,int &h,int &s,int &v) {
  float fr=r/255.0f, fg=g/255.0f, fb=b/255.0f;
  float maxc = max(fr,max(fg,fb)), minc = min(fr,min(fg,fb));
  v = (int)(maxc*255);
  float delta = maxc - minc;
  s = (maxc == 0) ? 0 : (int)((delta / maxc) * 255);
  if(delta == 0) { h = 0; return; }
  float hh;
  if(maxc == fr) hh = fmod((fg-fb)/delta, 6.0f);
  else if(maxc == fg) hh = ((fb-fr)/delta) + 2.0f;
  else hh = ((fr-fg)/delta) + 4.0f;
  hh *= 60.0f; if(hh < 0) hh += 360.0f;
  h = (int)hh;
}

String classifyColor(RGB c) {
  int h,s,v; rgbToHsv(c.r,c.g,c.b,h,s,v);
  if(v > 200 && s < 40) return "WHITE";
  if(v > 160 && s < 70) return "CREAM";
  if((h < 20 || h > 340)) return "RED";
  if(h>=10 && h<=45 && v < 160) return "BROWN";
  if(h>=180 && h<=260) return "BLUE";
  if(h>=60 && h<=170) return "GREEN";
  return "OTHER";
}

void moveServoToAngle(int angle) {
  servo.write(constrain(angle,0,180));
  delay(SERVO_MOVE_DELAY);
}

void performDispense(const String& color) {
  int angle = DispenseAngles::OTHER;
  if(color == "WHITE") angle = DispenseAngles::WHITE;
  else if(color == "CREAM") angle = DispenseAngles::CREAM;
  else if(color == "BROWN") angle = DispenseAngles::BROWN;
  else if(color == "RED") angle = DispenseAngles::RED;
  else if(color == "BLUE") angle = DispenseAngles::BLUE;
  else if(color == "GREEN") angle = DispenseAngles::GREEN;

  moveServoToAngle(angle);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(DISPENSE_OPEN_MS);
  digitalWrite(PIN_BUZZER, LOW);
  moveServoToAngle(DispenseAngles::HOME);
}

// Publish reported (incluye dispensed_* y last_dispense)
void publishShadowReport(const String& dispensedColor, int dispensedAngle, const RGB& c, const String& status) {
  StaticJsonDocument<384> doc;
  JsonObject state = doc.createNestedObject("state");
  JsonObject reported = state.createNestedObject("reported");

  reported["r"] = c.r; reported["g"] = c.g; reported["b"] = c.b;
  reported["dominant_color"] = classifyColor(c);
  reported["dispensed_color"] = dispensedColor;
  reported["dispensed_angle"] = dispensedAngle;
  reported["dispense_status"] = status;
  reported["command_id"] = commandCounter;
  reported["last_dispense"] = (unsigned long)time(nullptr);

  char buf[512]; serializeJson(doc, buf);
  client.publish(updateTopic.c_str(), buf);
}

void clearDesiredState() {
  StaticJsonDocument<128> doc;
  JsonObject state = doc.createNestedObject("state");
  JsonObject desired = state.createNestedObject("desired");
  desired["color"] = nullptr;
  desired["pill_hour"] = nullptr;
  desired["pill_minute"] = nullptr;
  char buf[128]; serializeJson(doc, buf);
  client.publish(updateTopic.c_str(), buf);
}

// ----- Non-blocking buzzer alarm support -----
unsigned long buzzerEndTs = 0;
bool buzzerActiveFlagForCurrentMinute = false;

void buzzerBeepNonBlocking(unsigned long ms) {
  digitalWrite(PIN_BUZZER, HIGH);
  buzzerEndTs = millis() + ms;
}

void buzzerLoopUpdate() {
  if (buzzerEndTs != 0 && millis() > buzzerEndTs) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerEndTs = 0;
  }
}

// MQTT callback (handle delta)
void callback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  if(!t.equals(deltaTopic)) return;
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if(err) {
    Serial.print("deserializeJson error: "); Serial.println(err.c_str());
    return;
  }

  JsonObject state = doc["state"];
  if(state.isNull()) return;

  if(state["pill_hour"]) {
    pillHour = state["pill_hour"].as<int>();
    Serial.printf("Updated pillHour=%d\n", pillHour);
  }
  if(state["pill_minute"]) {
    pillMinute = state["pill_minute"].as<int>();
    Serial.printf("Updated pillMinute=%d\n", pillMinute);
  }
  if(state["buzzer_enabled"]) {
    buzzerEnabled = state["buzzer_enabled"].as<bool>();
    Serial.printf("Updated buzzerEnabled=%d\n", buzzerEnabled ? 1 : 0);
  }

  if(state["color"]) {
    commandCounter++;
    String desiredColor = state["color"].as<String>();
    Serial.printf("Delta command received: %s\n", desiredColor.c_str());

    performDispense(desiredColor);

    RGB after = readAndNormalizeRGB();
    int angle = (desiredColor=="WHITE"?DispenseAngles::WHITE:
                 desiredColor=="CREAM"?DispenseAngles::CREAM:
                 desiredColor=="BROWN"?DispenseAngles::BROWN:
                 desiredColor=="RED"?DispenseAngles::RED:
                 desiredColor=="BLUE"?DispenseAngles::BLUE:
                 desiredColor=="GREEN"?DispenseAngles::GREEN:
                 DispenseAngles::OTHER);

    publishShadowReport(desiredColor, angle, after, "OK");
    clearDesiredState();
  }
}

// ---- Connect to WiFi ----
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  Serial.print("Connecting WiFi");
  while(WiFi.status() != WL_CONNECTED && millis()-start < 20000) {
    delay(500); Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed");
  }
}

// ---- Connect to AWS IoT (using embedded certs) ----
void connectToAWS() {
  // set certs (WiFiClientSecure)
  net.setCACert(rootCA);
  net.setCertificate(deviceCert);
  net.setPrivateKey(privateKey);
  Serial.println("Loaded certs from embedded strings and configured TLS.");

  client.setServer(AWS_IOT_ENDPOINT, AWS_IOT_PORT);
  client.setCallback(callback);

  unsigned long lastAttempt = 0;
  while(!client.connected()) {
    unsigned long now = millis();
    if (now - lastAttempt < 2000) {
      delay(100);
      continue;
    }
    lastAttempt = now;
    Serial.printf("Connecting to AWS IoT as %s ...\n", CLIENT_ID);
    if(client.connect(CLIENT_ID)) {
      updateTopic = String("$aws/things/") + THING_NAME + "/shadow/update";
      deltaTopic  = String("$aws/things/") + THING_NAME + "/shadow/update/delta";
      client.subscribe(deltaTopic.c_str());
      Serial.println("Connected to AWS IoT and subscribed to delta");
    } else {
      Serial.print("AWS connect failed rc=");
      Serial.println(client.state());
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  tcsBegin();
  servo.attach(PIN_SERVO);
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);

  if (! rtc.begin()) { Serial.println("Couldn't find RTC"); }
  else if (rtc.lostPower()) Serial.println("RTC lost power, set time or rely on NTP sync");
  connectToWiFi();
  connectToAWS();
}

void loop() {
  if(WiFi.status() != WL_CONNECTED) connectToWiFi();
  if(!client.connected()) connectToAWS();
  client.loop();

  buzzerLoopUpdate();

  DateTime now = rtc.now();
  if(now.isValid()) {
    if(buzzerEnabled && pillHour>=0 && pillMinute>=0) {
      if(now.hour() == pillHour && now.minute() == pillMinute && !alarmTriggered) {
        buzzerBeepNonBlocking(5000);
        alarmTriggered = true;
      }
      if(now.minute() != pillMinute) alarmTriggered = false;
    }
  }

  delay(10);
}


