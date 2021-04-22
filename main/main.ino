#include "FS.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LinkedList.h>

#define button D3

unsigned int ledStatus; /*Guardar o estado atual da lâmpada | 0 = acesa | 1 = apagada*/
char timerEnd[10];
char led; /*Variável para guardar o estado da lâmpada enviado pelo MQTT.*/

/*Horário que a lâmpada ascendeu e apagou respectivamente.*/
unsigned int timeOn, timeOff;
/*diff: Tempo que a lâmpada permaceu ligada. | accTimeOn: Tempo que a lâmpada permaceu ligada durante um dado momento.*/
float diff, accTimeOn = 0.0;
/*Flag para determinar se o temporizador foi ativado.*/
boolean flagTimer = false;
/*Flag para determinar se o temporizador chegou no final.*/
boolean flagTimeOn = false;
/*Flag para determinar se já está no horário de fim.*/
boolean flagScheduleOn = false;

LinkedList<String> beginList = LinkedList<String>(); /*Lista com os horários de início.*/
LinkedList<String> endList = LinkedList<String>(); /*Lista com os horários de fim.*/
LinkedList<int> ledControlList = LinkedList<int>(); /*Lista com os estados do LED.*/

String localTime;//Váriavel que armazenara o horario do NTP.
WiFiUDP udp;//Cria um objeto "UDP".
NTPClient ntp(udp, "b.ntp.br", -3 * 3600, 60000);//Cria um objeto "NTP" com as configurações.

/*-- Credenciais do WiFi --*/
const char* ssid = "nomeDaRede"; /*Nome da Rede WiFi*/
const char* password = "senhaDaRede"; /*Senha da Rede WiFi*/

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

const char* AWS_endpoint = "seuEndpoint"; //Endpoint do dispositivo na AWS.

/*-- Função responsável pela comunicação da AWS IoT Core com a placa ESP8266 --*/
void callback(char* topic, byte* payload, unsigned int length) {
  char **data; /*Matriz para armazenar cada um dos dados enviados na publicação.*/

  Serial.println("");
  Serial.print("Tópico [");
  Serial.print(topic);
  Serial.print("] ");

  /*-- Exibir a mensagem publicada no Monitor Serial --*/
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }

  if(strcmp(topic, "lampInTopic") == 0) {
    /*Alocando dinâmicamente a matriz.*/
    data = allocateMatrix(1, 1);
    /*Salvando os dados enviados na publicação na matriz.*/
    data = readPublicationMQTT(payload, length, data);
    /*Definindo o estado da lâmpada*/
    led = data[0][0];

    if(led == '0' || led == '1'){    
      if(led==49) { // 49 é a representação ASCII para o número 1
        digitalWrite(LED_BUILTIN, HIGH);
        ledStatus = 1;
        Serial.println("Lâmpada desligada!");

        timeOff = millis();
        diff = (timeOff - timeOn) / 1000;
        accTimeOn += diff;
      }
      else if(led==48) { // 48 é a representação ASCII para o número 0
        digitalWrite(LED_BUILTIN, LOW);
        ledStatus = 0;
        Serial.println("Lâmpada ligada!");

        timeOn = millis();
       }          
      Serial.println();
      returnMessage("success-lamp");
    } else {
      Serial.println("Erro! Dado inválido.");
      returnMessage("error-lamp");
    }
      /*Liberando a matriz alocada dinamicamente.*/
      freeMatrix(data, 1);
  } else if(strcmp(topic, "timerInTopic") == 0){
    flagTimer = true;
    flagTimeOn = true;
    /*Guardando o horário atual.*/
    localTime = ntp.getFormattedTime();
    /*Alocando dinâmicamente a matriz.*/
    data = allocateMatrix(2, 10);
    /*Salvando os dados enviados na publicação na matriz.*/
    data = readPublicationMQTT(payload, length, data);
    
    /*Definindo o estado da lâmpada*/
    led = data[0][0];

    if((led == '0' || led == '1') && (strchr(data[1], 'h') != NULL && strchr(data[1], 'm') != NULL && strchr(data[1], 's') != NULL)){
      /*Verificando o que foi passado para definir o estado do LED.*/
      if(led == 48){
        ledStatus = 0;
      } else if (led == 49) {
        ledStatus = 1;
      }
  
      /*Defifindo um novo timer com base no tempo passado.*/
      setTimer(data[1], localTime);
      returnMessage("success-timer");
    } else {
      returnMessage("error-timer");
    }
    /*Liberando a matriz alocada dinamicamente.*/
    freeMatrix(data, 10);
  } else if(strcmp(topic, "scheduleInTopic") == 0){
    flagScheduleOn = true;
    /*Guardando o horário atual.*/
    localTime = ntp.getFormattedTime();
    /*Alocando dinâmicamente a matriz.*/
    data = allocateMatrix(3, 10);
    /*Salvando os dados enviados na publicação na matriz.*/
    data = readPublicationMQTT(payload, length, data);
    /*Definindo o estado da lâmpada*/
    led = data[0][0];

    if((led == '0' || led == '1') && (strchr(data[1], 'h') != NULL && strchr(data[1], 'm') != NULL && strchr(data[1], 's') != NULL) && (strchr(data[2], 'h') != NULL && strchr(data[2], 'm') != NULL && strchr(data[2], 's') != NULL)){

      /*Verificando o que foi passado para definir o estado do LED para salvar na lista.*/
      if(led == 48){
        ledControlList.add(0);
      } else if (led == 49) {
        ledControlList.add(1);
      }
  
      /*Salvando os horários publicados nas suas devidas listas.*/
      beginList.add(setSchedule(data[1]));
      endList.add(setSchedule(data[2]));
      
      returnMessage("success-schedule");
    } else {
      returnMessage("error-schedule");
    }

    /*Liberando a matriz alocada dinamicamente.*/
    freeMatrix(data, 10);
  } else if(strcmp(topic, "historicInTopic") == 0) {
    /*Alocando dinâmicamente a matriz.*/
    data = allocateMatrix(1, 10);
    /*Salvando os dados enviados na publicação na matriz.*/
    data = readPublicationMQTT(payload, length, data);

    if(strcmp(data[0], "refresh") == 0) {
      returnDataHistoric();
    }

    /*Liberando a matriz alocada dinamicamente.*/
    freeMatrix(data, 10);
  } else {
    Serial.println("Erro! Tópico não encontrado.");
  }

}
WiFiClientSecure espClient;
PubSubClient client(AWS_endpoint, 8883, callback, espClient); //Realizando a comunicação MQTT da placa com a AWS, através da porta 8883

/*-- Função para realizar a conexão à rede WiFI. --*/
void setup_wifi() {
  delay(10);
  espClient.setBufferSizes(512, 512);
  Serial.println();
  Serial.print("Conectando em: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado com sucesso!");

  timeClient.begin();
  while(!timeClient.update()){
    timeClient.forceUpdate();
  }

  espClient.setX509Time(timeClient.getEpochTime());
}

/*-- Função para reconectar com o protocolo MQTT. --*/
void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentativa de conexão MQTT...");
    
    /*Tentativa de conexão MQTT*/
    if (client.connect("ESPthing")) {
      Serial.println("Conectado!");
      client.subscribe("lampInTopic");
      client.subscribe("timerInTopic");
      client.subscribe("scheduleInTopic");
      client.subscribe("historicInTopic");
    } else {
      Serial.print("Falhou! Erro:");
      Serial.print(client.state());
      Serial.println(" Tentando novamente em 5 segundos");

      /*Verificação do certificado SSL*/
      char buf[256];
      espClient.getLastSSLError(buf,256);
      Serial.print("SSL erro: ");
      Serial.println(buf);

      /* Esperar 5 segundos antes de tentar novamente. */
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial.setDebugOutput(true);
  
  /* Definindo o LED da placa como Output. */
  pinMode(LED_BUILTIN, OUTPUT);

  /* Definindo o botão da placa como Input. */
  pinMode(button, INPUT_PULLUP);
  
  setup_wifi();
  
  delay(1000);
  
  ntp.begin();
  ntp.forceUpdate();//Força o Update.
  
  if (!SPIFFS.begin()) {
    Serial.println("Falha na montagem do arquivo de sistema");
    return;
  }

  /*Iniciando a lâmpada como desligada.*/
  digitalWrite(LED_BUILTIN, HIGH);
  ledStatus = 1; 

  /*-- Carregando os certificados na placa --*/
  loadCertificates();
}

void loop() {
  int btnVal = digitalRead(button);
  localTime = ntp.getFormattedTime();
  
  if (!client.connected()) {
    reconnect();
  }

  /*Se o botão for pressionado*/
  if(btnVal == 0){
    delay(250);
    
    if(ledStatus == 1){ /*Se tiver desligada, muda para acesa.*/
      digitalWrite(LED_BUILTIN, LOW);
      ledStatus = 0;

      timeOn = millis();
      /*Atualizando o estado da lampâda diretamente no Banco de Dados, utilizando o Lambda Function*/
      client.publish("lampOutTopic", "{\"LED_Control\": 1}");
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      ledStatus = 1;
      
      timeOff = millis();
      diff = (timeOff - timeOn) / 1000;
      accTimeOn += diff;
      /*Atualizando o estado da lampâda diretamente no Banco de Dados, utilizando o Lambda Function*/
      client.publish("lampOutTopic", "{\"LED_Control\": 0}");
    }
  }

  /*Alterar o estado conforme o  temporizador.*/
  if(ledStatus == 1 && flagTimer) { // DESLIGAR pelo tempo x
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);

    if(flagTimeOn){
      timeOff = millis();
      
      diff = (timeOff - timeOn) / 1000;
      accTimeOn += (diff + 1); // Por conta dos delays, o tempo que a lâmpada permace ligada tem um atraso de 1 segundo.
      
      flagTimeOn = false;
    }

    if(localTime == timerEnd && led == 49) {
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      ledStatus = 0;

      timeOn = millis();
      
      flagTimer = false;
      /*Atualizando o estado da lampâda diretamente no Banco de Dados, utilizando o Lambda Function*/
      client.publish("lampOutTopic", "{\"LED_Control\": 1}");
    }
  } else if(ledStatus == 0 && flagTimer) { // LIGAR pelo tempo x
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);

    if(flagTimeOn){
      timeOn = millis();
      flagTimeOn = false;
    }

    if(localTime == timerEnd && led == 48) {
      delay(100);
      digitalWrite(LED_BUILTIN, HIGH);
      ledStatus = 1;

      timeOff = millis();
      diff = (timeOff - timeOn) / 1000;
      accTimeOn += (diff + 1); // Por conta dos delays, o tempo que a lâmpada permace ligada tem um atraso de 1 segundo.
      
      flagTimer = false;
      /*Atualizando o estado da lampâda diretamente no Banco de Dados, utilizando o Lambda Function*/
      client.publish("lampOutTopic", "{\"LED_Control\": 0}");
    }
  }

  for(int i = 0; i < beginList.size(); i++){
    /*Condição para mudar o estado da lâmpada quando chegar o tempo de início*/
    if(localTime == beginList.get(i)) { /*Fazer a condição lá do led em ASCII*/
      delay(100);
      if(ledControlList.get(i) == 0){
        digitalWrite(LED_BUILTIN, LOW);
        ledStatus = 0;
        
        timeOn = millis();
        /*Atualizando o estado da lampâda diretamente no Banco de Dados, utilizando o Lambda Function*/
        client.publish("lampOutTopic", "{\"LED_Control\": 1}");
      } else {
        digitalWrite(LED_BUILTIN, HIGH);
        ledStatus = 1;

        if (flagScheduleOn){
          timeOff = millis();
          diff = (timeOff - timeOn) / 1000;
          accTimeOn += (diff + 1); // Por conta dos delays, o tempo que a lâmpada permace ligada tem um atraso de 1 segundo.

          flagScheduleOn = false;
          /*Atualizando o estado da lampâda diretamente no Banco de Dados, utilizando o Lambda Function*/
          client.publish("lampOutTopic", "{\"LED_Control\": 0}");
        }
      }
    }
    
    /*Condição para mudar o estado da lâmpada para quando o tempo de fim*/
    if(localTime == endList.get(i)) { /*Fazer a condição lá do led em ASCII*/
      delay(100);
      if(ledControlList.get(i) == 0){
        digitalWrite(LED_BUILTIN, HIGH);
        ledStatus = 1;

        if (flagScheduleOn){
          timeOff = millis();
          diff = (timeOff - timeOn) / 1000;
          accTimeOn += (diff + 1); // Por conta dos delays, o tempo que a lâmpada permace ligada tem um atraso de 1 segundo.

          flagScheduleOn = false;
          /*Atualizando o estado da lampâda diretamente no Banco de Dados, utilizando o Lambda Function*/
          client.publish("lampOutTopic", "{\"LED_Control\": 0}");
        }
      } else {
        digitalWrite(LED_BUILTIN, LOW);
        ledStatus = 0;

        timeOn = millis();
        /*Atualizando o estado da lampâda diretamente no Banco de Dados, utilizando o Lambda Function*/
        client.publish("lampOutTopic", "{\"LED_Control\": 1}");
      }
    }
  }
  
  client.loop();
}

/*-- Função para enviar os dados do histórico --*/
void returnDataHistoric() {
  char message[15];
  sprintf(message, "%f", accTimeOn);

  delay(100);
  client.publish("historicOutTopic", message);
}

/*-- Função para retornar a mensagem de erro ou sucesso ao publicar pelo serviço web. --*/
void returnMessage(String message){
  delay(100);
  
  if(message == "success-lamp")
    client.publish("lampMessage", "success");
  else if(message == "error-lamp")
    client.publish("lampMessage", "error");
  else if(message == "success-timer")
    client.publish("timerOutTopic", "success");
  else if(message == "error-timer")
    client.publish("timerOutTopic", "error");
  else if(message == "success-schedule")
    client.publish("scheduleOutTopic", "success");
  else if(message == "error-schedule")
    client.publish("scheduleOutTopic", "error");
}

/*-- Função para leitura dos dados que foram publicados pelo protocolo MQTT. --*/
char **readPublicationMQTT(byte* payload, unsigned int length, char** response){
  int j = 0, l = 0, k = 0;
  
  for (int i = 0; i < length; i++) {
    if((char)payload[i] == ':'){
      k = i + 2;
      l = 0;
      
      while((char)payload[k] != ','){
        response[j][l] = (char)payload[k];
        k++;
        l++;
      }  
      j++;
    }
  }
  
  return response;
}

/*-- Função para definir um novo timer. --*/
void setTimer(String data, String localTime){
  String timer;
  String temp = "", temp2 = "";
  int timerSecondsInt, localTimeSecondsInt;
  int timerMinutesInt, localTimeMinutesInt;
  int timerHourInt, localTimeHourInt;
  int hourEnd = 0, minutesEnd = 0, secondsEnd = 0;
  
  timer = data;

  timer.replace("h", ":");
  timer.replace("m", ":");
  timer.remove(8, 1);

  temp += timer[6];
  temp += timer[7];

  temp2 += localTime[6];
  temp2 += localTime[7];
  
  timerSecondsInt = temp.toInt();
  localTimeSecondsInt = temp2.toInt();

  temp = "";
  temp2 = "";

  temp = timer[3];
  temp += timer[4];

  temp2 = localTime[3];
  temp2 += localTime[4];
  
  timerMinutesInt = temp.toInt();
  localTimeMinutesInt = temp2.toInt();

  temp = "";
  temp2 = "";

  temp = timer[0];
  temp += timer[1];

  temp2 = localTime[0];
  temp2 += localTime[1];
  
  timerHourInt = temp.toInt();
  localTimeHourInt = temp2.toInt();

  secondsEnd = timerSecondsInt + localTimeSecondsInt;
  
  if (secondsEnd > 59){
    minutesEnd = 1;
    secondsEnd = secondsEnd - 60;
  }

  minutesEnd = minutesEnd + timerMinutesInt + localTimeMinutesInt;

  if (minutesEnd > 59){
    hourEnd = 1;
    minutesEnd = minutesEnd - 60;
  }

  hourEnd = hourEnd + timerHourInt + localTimeHourInt;

  if (hourEnd > 23){
    hourEnd = hourEnd - 24;
  }

  if(hourEnd < 10){
    sprintf(timerEnd, "0%d", hourEnd);
  } else {
    sprintf(timerEnd, "%d", hourEnd);
  }

  if(minutesEnd < 10){
    sprintf(timerEnd, "%s:0%d", timerEnd, minutesEnd);
  } else {
    sprintf(timerEnd, "%s:%d", timerEnd, minutesEnd);
  }

  if(secondsEnd < 10){
    sprintf(timerEnd, "%s:0%d", timerEnd, secondsEnd);
  } else {
    sprintf(timerEnd, "%s:%d", timerEnd, secondsEnd);
  }
}

/*-- Função formatação do horário. --*/
String setSchedule(String timeSchedule){
  timeSchedule.replace("h", ":");
  timeSchedule.replace("m", ":");
  timeSchedule.remove(8, 1);

  return timeSchedule;
}

/*-- Função para alocar dinamicamente o tamanho da matriz --*/
char **allocateMatrix(int row, int col){
  char **matrix;
  int i, j;

  matrix = (char**)malloc(sizeof(char*) * row);
  
  for(i = 0; i < col; i++){
    matrix[i] = (char *)malloc(sizeof(char) * col);
  }

  for (i = 0; i < row; i++){
    for(j = 0; j < col; j++){
      matrix[i][j] = '\0';
    }
  }
  
  return matrix;
}

/*-- Função para liberar o espaço na memória que foi alocado dinamicamente --*/
void freeMatrix(char **matrix, int col){
  int i;
  
  for(i = 0; i < col; i++)
    free(matrix[i]);
    
  free(matrix);
}

/*-- Função para abrir e carregar todos os certificados. --*/
void loadCertificates() {
  /*-- Carregando cert.der --*/
  File cert = SPIFFS.open("/cert.der", "r"); 
  
  if (!cert)
    Serial.println("Falha ao tentar abrir o arquivo cert.der");
  else
    Serial.println("Sucesso ao abrir o arquivo cert.der");

  delay(1000);

  if (espClient.loadCertificate(cert))
    Serial.println("Sucesso ao carregar arquivo cert.der");
  else
    Serial.println("Erro ao carregar arquivo cert.der");

  /*-- Carregando private.der --*/
  File private_key = SPIFFS.open("/private.der", "r");
  
  if (!private_key)
    Serial.println("Falha ao tentar abrir o arquivo private.der");
  else
    Serial.println("Sucesso ao abrir o arquivo private.der");

  delay(1000);

  if (espClient.loadPrivateKey(private_key))
    Serial.println("Sucesso ao carregar arquivo private.der");
  else
    Serial.println("Erro ao carregar arquivo private.der");
    
  /*-- Carregando car.der --*/
  File ca = SPIFFS.open("/ca.der", "r");
  
  if (!ca)
    Serial.println("Falha ao tentar abrir o arquivo ca.der");
  else
    Serial.println("Sucesso ao abrir o arquivo ca.der");

  delay(1000);

  if(espClient.loadCACert(ca))
   Serial.println("Sucesso ao carregar arquivo ca.der");
  else
    Serial.println("Erro ao carregar arquivo ca.der");
}
