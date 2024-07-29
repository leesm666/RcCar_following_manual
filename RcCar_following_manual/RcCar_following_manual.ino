#include <WebSocketsServer.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"


#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22


#define RightMotor_1_pin    2 // In1
#define RightMotor_2_pin    14 // In2
#define LeftMotor_3_pin    15 //In3
#define LeftMotor_4_pin    13 //In4
#define RightMotor_E_pin  12
#define LeftMotor_E_pin  12

int L_MotorSpeed = 115;                        // 왼쪽 모터 속도
int R_MotorSpeed = 115;                        // 오른쪽 모터 속도


// Set your Static IP address
IPAddress local_IP(172, 20, 10, 10);
// Set your Gateway IP address
IPAddress gateway(172, 20, 10, 2); // 공유기 IP
IPAddress subnet(255, 255, 0, 0);


const char* ssid = "iPhone00"; // 와이파이 주소 (2.4GHz)
const char* password = "dl794613!"; // 와이파이 비밀번호

uint32_t duty; // Websocket 설정을 위한 변수

int count; // 제자리 회전 
int count2; // 자동모드로 변경하는 변수
int mode = 0; // 0 자동, 1 수동

// 웹소켓 이벤트 핸들러 함수
WebSocketsServer webSocket = WebSocketsServer(81);
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if(type == WStype_TEXT){  // 웹소켓 이벤트 타입이 텍스트인 경우
    String cmd = (char*)payload; // 페이로드를 문자열로 변환하여 cmd에 저장
    Serial.println(cmd);

    if(mode==0){ // 자동모드

      if(cmd == "Unrecognizable"){ //사람 감지가 안되었을 때
        count++;
        motor_stop();
      }

      else{ //사람 감지가 되었을 때
        count = 0;
        int widData = cmd.substring(1,4).toInt(); // 바운딩박스 가로값
        int xmidData =  cmd.substring(5,8).toInt(); // 바운딩박스 X좌표 중심 값

        if(widData < 450 && xmidData < 410 && xmidData > 250){
          Serial.println("xid400이하");
          motor_role(HIGH,HIGH); // 직진
        }
        else if(xmidData >= 410 && widData >= 450){ 
          Serial.println("제자리우회전");
          motor_role(HIGH, LOW);
          
        }
        else if(xmidData >= 410 && widData < 450){  
          Serial.println("제자리우회전");
          motor_role(HIGH, LOW);
          
        }
        else if(xmidData <= 250 && widData >= 450){
          Serial.println("제자리 좌회전");
          motor_role(LOW, HIGH); 
        }
        else if(xmidData <= 250 && widData < 450){
          Serial.println("제자리 좌회전");
          motor_role(LOW, HIGH); 
        }
        else if(widData >= 450 && xmidData < 410 && xmidData > 250){ 
        motor_stop();
        }
      }
      
      if(count==100){ // 사람이 계속 감지 안되었을때 제자리 돌기

        count=0;
        motor_role(LOW, HIGH); 
        delay(500);
        motor_stop();
      }
    }

    if(mode==1){ //수동 조작 모드
      if(cmd == "Unrecognizable"){
        count2++;
      }

      if(count2 == 200){ // 수동조작 모드 -> 자동모드로 변경
        Serial.println("count==200");
        mode=0;
        count2=0;
      }
    }
  }
}


void motor_role(int R_motor, int L_motor){ //모터 조작
   digitalWrite(RightMotor_1_pin, R_motor);
   digitalWrite(RightMotor_2_pin, !R_motor);
   digitalWrite(LeftMotor_3_pin, L_motor);
   digitalWrite(LeftMotor_4_pin, !L_motor);
   
   analogWrite(RightMotor_E_pin, R_MotorSpeed);  // 우측 모터 속도값
   analogWrite(LeftMotor_E_pin, L_MotorSpeed);  // 좌측 모터 속도값  
}

void motor_stop(){ //모터 정지
   digitalWrite(RightMotor_1_pin, 0);
   digitalWrite(RightMotor_2_pin, 0);
   digitalWrite(LeftMotor_3_pin, 0);
   digitalWrite(LeftMotor_4_pin, 0);

   analogWrite(RightMotor_E_pin, 0);  
   analogWrite(LeftMotor_E_pin, 0);                        
}


#define PART_BOUNDARY "123456789000000000000987654321"  // 멀티파트 경계
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY; // 멀티파트 스트림의 컨텐츠 타입을 설정
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n"; // 스트림의 경계를 설정
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n"; // 이미지 데이터의 부분 헤더를 설정 (JPEG 이미지, 길이를 포함)
httpd_handle_t stream_httpd = NULL; // HTTP 서버 핸들을 초기화

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
 <!DOCTYPE HTML><html> 
     <head>
       <meta charset="utf-8">
       <meta name="viewport" content="width=device-width, initial-scale=1"> 
       <script src="https:\/\/ajax.googleapis.com/ajax/libs/jquery/1.8.0/jquery.min.js"></script>

       <script src="https:\/\/cdn.jsdelivr.net/npm/@tensorflow/tfjs@1.0.1"> </script>

       <script src="https:\/\/cdn.jsdelivr.net/npm/@tensorflow-models/coco-ssd"> </script> 
       <style>
      body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}
      table { margin-left: auto; margin-right: auto; }
      td { padding: 8 px; }
      .button {
        background-color: #2f4468;
        border: none;
        color: white;
        padding: 10px 20px;
        text-align: center;
        text-decoration: none;
        display: inline-block;
        font-size: 18px;
        margin: 6px 3px;
        cursor: pointer;
        -webkit-touch-callout: none;
        -webkit-user-select: none;
        -khtml-user-select: none;
        -moz-user-select: none;
        -ms-user-select: none;
        user-select: none;
        -webkit-tap-highlight-color: rgba(0,0,0,0);
      }
    </style> 
     </head>
     <body>
      
      <h2>ESP32 Object Following Camera</h2>

      <img id="camera_image" src="" style="display:none">

      <canvas id="canvas" width="0" height="0"></canvas>  

      <table>
        <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('forward');" ontouchstart="toggleCheckbox('forward');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Forward</button></td></tr>
        <tr><td align="center"><button class="button" onmousedown="toggleCheckbox('left');" ontouchstart="toggleCheckbox('left');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Left</button></td><td align="center"><button class="button" onmousedown="toggleCheckbox('stop');" ontouchstart="toggleCheckbox('stop');">Stop</button></td>
        <td align="center"><button class="button" onmousedown="toggleCheckbox('right');" ontouchstart="toggleCheckbox('right');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Right</button></td></tr>
        <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('backward');" ontouchstart="toggleCheckbox('backward');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Backward</button></td></tr>                   
      </table>

      <iframe id="ifr" style="display:none"></iframe>

      <div id="result" style="color:red"><div>

     </body>  
    </html>
    <script>
      function toggleCheckbox(x) {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/action?go=" + x, true);
        xhr.send();
      }
        const ip = window.location.protocol + "//" + window.location.hostname;
        const ws = new WebSocket(ip.replace("http", "ws") + ":81");
        ws.onopen = function (msg) {
            console.log("Connected");
        };
        ws.onclose = function (msg) {
            console.log("Closed");
        };
        ws.onmessage = function (msg) {
            console.log(msg.data);
        };

         var context = canvas.getContext("2d");

         var flag = 0;

         var Model;

         camera_image.onload = function (event) 
         {
           var timestamp = new Date().getTime();
           canvas.setAttribute("width", camera_image.width);
           canvas.setAttribute("height", camera_image.height);
           context.translate((canvas.width + camera_image.width) / 2, 0);
           context.scale(-1, 1);
           context.drawImage(camera_image, 0, 0, camera_image.width, camera_image.height);
           context.setTransform(1, 0, 0, 1, 0, 0);
           Model.detect(canvas).then(Predictions => 
           {
             var s = (canvas.width>canvas.height)?canvas.width:canvas.height;
             var trackState = 0;
             var x, y, width, height;

             if (Predictions.length>0) 
             {
               result.innerHTML = "";

               for (var i=0;i<Predictions.length;i++) 
               {
                 if (Predictions[i].class=="person"&&Predictions[i].score>0.5&&trackState == 0) 

                 {   

                   const x = Predictions[i].bbox[0];
                   const y = Predictions[i].bbox[1];
                   const width = Predictions[i].bbox[2];
                   const height = Predictions[i].bbox[3];

                   context.lineWidth = "1";
                   context.strokeStyle = "#0000FF";
                   context.beginPath();
                   context.rect(x, y, width, height);
                   context.stroke(); 
                   context.lineWidth = "2";
                   context.fillStyle = "red";
                   context.font = "20px Arial";
                   context.fillText(Predictions[i].class, x, y);

                   result.innerHTML+= Math.round(Predictions[i].score*100 ) + "% , LEFT : " + Math.round(x) + ", TOP : "+Math.round(y)+", 가로 : "+Math.round(width)+", 세로 : "+Math.round(height)+"<br>";
                   
                   trackState = 1;
                   var midX = Math.round(x)+Math.round(width)/2;
                   result.innerHTML+= "X중앙값" + midX + "<br>";

                   var midY = Math.round(y)+Math.round(height)/2;

                   result.innerHTML+= "Y중앙값" + midY + "<br>";
                   const command = String(Math.round(width)).padStart(4, "0") + String(Math.round(midX)).padStart(4, "0")

                   
                   ws.send(command);
                   
                   ifr.src = location.origin+"/object_data?servo1=" + midX + "&servo2=" + midY;
                 }
               }
             }
             else 
             {
               result.innerHTML = "Unrecognizable";
               ws.send("Unrecognizable");
             }
          });
           camera_image.src="/capture?t=" + timestamp;
         }     
         window.onload = function () 
         { 
           result.innerHTML = "Please wait for loading model.";
      cocoSsd.load().then
           (cocoSsd_Model =>
             {
               var timestamp = new Date().getTime();
               Model = cocoSsd_Model;
               result.innerHTML = "";
               camera_image.src="/capture?t=" + timestamp;
             }
           );       
         }    

    </script>

)rawliteral";

// 구조체 정의
typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

//JPEG 데이터를 HTTP 응답의 일부로 청크 단위로 전송하는 함수
static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index)
    {
        j->len = 0;
    }
    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
    {
        return 0;
    }
    j->len += len;
    return len;
}

//인덱스 페이지에 대한 HTTP 요청을 처리하는 함수 
static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

// URL 쿼리 스트링 처리, 모터 제
static esp_err_t cmd_handler(httpd_req_t *req){
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};
  
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if(!buf){
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  sensor_t * s = esp_camera_sensor_get();
  int res = 0;
  
  if(!strcmp(variable, "forward")) {
    Serial.println("Forward");
    mode=1;
   motor_role(HIGH,HIGH);
  }
  else if(!strcmp(variable, "left")) {
    Serial.println("Left");
    mode=1;
    motor_role(HIGH,LOW);
    
  }
  else if(!strcmp(variable, "right")) {
    Serial.println("Right");
    mode=1;
    motor_role(LOW,HIGH);
  }
  else if(!strcmp(variable, "backward")) {
    Serial.println("Backward");
    mode=1;
    motor_role(LOW,LOW);
  }
  else if(!strcmp(variable, "stop")) {
    Serial.println("Stop");
    mode=1;
    motor_stop();
  }
  else {
    res = -1;
  }
  if(res){
    return httpd_resp_send_500(req);
  }
  // HTTP 응답 설정
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t *fb = NULL; //카메라 프레임 버퍼에 대한 포인터.
    esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    int64_t fr_start = esp_timer_get_time();
#endif

#if CONFIG_LED_ILLUMINATOR_ENABLED
    enable_led(true);
    vTaskDelay(150 / portTICK_PERIOD_MS); 
    fb = esp_camera_fb_get();             
    enable_led(false);
#else
    fb = esp_camera_fb_get();
#endif

    if (!fb) //프레임 캡처가 실패시 에러 로그를 남기고 HTTP 500 에러 응답을 전송
    {
        log_e("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // HTTP 응답 헤더 설정
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char ts[32];
    snprintf(ts, 32, "%ld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
    httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

// JPEG 전송
// 얼굴 인식 처리
#if CONFIG_ESP_FACE_DETECT_ENABLED
    size_t out_len, out_width, out_height;
    uint8_t *out_buf;
    bool s;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    bool detected = false;
#endif
    int face_id = 0;
    if (!detection_enabled || fb->width > 400)
    {
#endif
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO //로그 및 응답 반환
        size_t fb_len = 0;
#endif
        if (fb->format == PIXFORMAT_JPEG)
        {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
            fb_len = fb->len;
#endif
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        }
        else
        {
            jpg_chunking_t jchunk = {req, 0};
            res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
            httpd_resp_send_chunk(req, NULL, 0);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
            fb_len = jchunk.len;
#endif
        }
        esp_camera_fb_return(fb);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        int64_t fr_end = esp_timer_get_time();
#endif
        log_i("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
        return res;
#if CONFIG_ESP_FACE_DETECT_ENABLED
    }

    jpg_chunking_t jchunk = {req, 0};

    if (fb->format == PIXFORMAT_RGB565
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
     && !recognition_enabled
#endif
     ){
#if TWO_STAGE
        HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
        HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
        std::list<dl::detect::result_t> &candidates = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
        std::list<dl::detect::result_t> &results = s2.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
#else
        HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
        std::list<dl::detect::result_t> &results = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
#endif
        if (results.size() > 0) {
            fb_data_t rfb;
            rfb.width = fb->width;
            rfb.height = fb->height;
            rfb.data = fb->buf;
            rfb.bytes_per_pixel = 2;
            rfb.format = FB_RGB565;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
            detected = true;
#endif
            draw_face_boxes(&rfb, &results, face_id);
        }
        s = fmt2jpg_cb(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 90, jpg_encode_stream, &jchunk);
        esp_camera_fb_return(fb);
    } else
    {
        out_len = fb->width * fb->height * 3;
        out_width = fb->width;
        out_height = fb->height;
        out_buf = (uint8_t*)malloc(out_len);
        if (!out_buf) {
            log_e("out_buf malloc failed");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
        esp_camera_fb_return(fb);
        if (!s) {
            free(out_buf);
            log_e("To rgb888 failed");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        fb_data_t rfb;
        rfb.width = out_width;
        rfb.height = out_height;
        rfb.data = out_buf;
        rfb.bytes_per_pixel = 3;
        rfb.format = FB_BGR888;

#if TWO_STAGE
        HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
        HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
        std::list<dl::detect::result_t> &candidates = s1.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3});
        std::list<dl::detect::result_t> &results = s2.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3}, candidates);
#else
        HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
        std::list<dl::detect::result_t> &results = s1.infer((uint8_t *)out_buf, {(int)out_height, (int)out_width, 3});
#endif

        if (results.size() > 0) {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
            detected = true;
#endif
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
            if (recognition_enabled) {
                face_id = run_face_recognition(&rfb, &results);
            }
#endif
            draw_face_boxes(&rfb, &results, face_id);
        }

        s = fmt2jpg_cb(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, jpg_encode_stream, &jchunk);
        free(out_buf);
    }

    if (!s) {
        log_e("JPEG compression failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    int64_t fr_end = esp_timer_get_time();
#endif
    log_i("FACE: %uB %ums %s%d", (uint32_t)(jchunk.len), (uint32_t)((fr_end - fr_start) / 1000), detected ? "DETECTED " : "", face_id);
    return res;
#endif
}

// 카메라서버 시작 함수
void startCameraServer(){
  // httpd_config_t 구조체를 사용하여 기본 웹 서버 설정
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80; // 서버 포트

  httpd_uri_t index_uri = { //index 핸들러로 '/' 경로로 들어오는 GET 요청을 index_handler 함수처리
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t cmd_uri = { //command 핸들러로 '/' 경로로 들어오는 GET 요청 cmd_handler 함수처리
    .uri       = "/action",
    .method    = HTTP_GET,
    .handler   = cmd_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t capture_uri = { //capture 핸들러로 '/' 경로로 들어오는 GET 요청 capture_handler 함수처리
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
  };

  // 서버 시작 및 URI 핸들러 등록
  if (httpd_start(&stream_httpd, &config) == ESP_OK) { 
    // 서버 시작이 성공하면 URI  핸들러를 서버에 등록
    httpd_register_uri_handler(stream_httpd, &index_uri);
    httpd_register_uri_handler(stream_httpd, &capture_uri);
    httpd_register_uri_handler(stream_httpd, &cmd_uri);
  }
}

void setup() {
  
  pinMode(RightMotor_1_pin, OUTPUT);
  pinMode(RightMotor_2_pin, OUTPUT);
  pinMode(LeftMotor_3_pin, OUTPUT);
  pinMode(LeftMotor_4_pin, OUTPUT);
  pinMode(RightMotor_E_pin, OUTPUT);
  pinMode(LeftMotor_E_pin, OUTPUT);

  // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
 
  //시리얼 통신을 115200 보드레이트로 시작하고, 디버그 출력을 비활성화
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  
  //카메라 설정 구성(GPIO 핀, 픽셀로맷 JPEG품질, 프레임 버퍼, 프레임 크기 등)
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 25;
  config.fb_count = 2;
  
  // 카메라 초기화, 실패시 에러 메시지 출력 
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Wi-Fi 설정을 구성하고, 네트워크에 연결을 시도 후 연결이 성공하면 IP 주소를 출력
  if(!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }
  // Wi-Fi 연결 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.print(WiFi.localIP());
  Serial.println("");
  
  // 스트리밍 웹 서버 실행함수 호출
  startCameraServer();

  // 웹 소켓 서버 실행
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
}

void loop() {
  webSocket.loop();
}
